# MIT License
#
# Copyright (c) 2018-2026 Jakub Melka and Contributors
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Generator of the catalog of the OCR language models of Tesseract.

The script produces these files of the repository:

    ocr/catalog/tesseract-catalog.json       all models of the profiles (compiled into
                                             Pdf4QtLibCore by ocr.qrc)
    ocr/tesseract/<profile>/manifest.json    models distributed with the application
    ocr/tesseract/<profile>/LICENSE          license of the model repository
    ocr/tesseract/<profile>/tessdata/*.tar.xz    the model files themselves

The model files are stored in the repository as xz compressed archives, one model
per archive, because a traineddata file compresses to about a third of its size.
CMake extracts them into the build tree, nothing is downloaded during a build.

The profiles are fast (tessdata_fast), standard (tessdata) and best (tessdata_best).
Every model repository is pinned to a commit. The list of the files of the commit
is read from the GitHub API, every *.traineddata file is downloaded from
raw.githubusercontent.com, verified against the size and the git blob SHA-1 of the
commit, and its SHA-256 is stored in the catalog. The application never downloads
a catalog from the network, so the checksums can be changed only by this script.
A model without the LSTM component (legacy only data, for example equ) is not
written into the catalog, because the application uses the LSTM engine. A symbolic
link of the repository (frk of tessdata_fast points to deu_latf) is left out too,
its raw download is only the name of the target.

The downloads are kept in a cache directory (about 3 GB for all profiles), a file
of the cache is downloaded again only when it does not match the commit. The field
"generated" of the catalog changes only when something else has changed, so
a repeated run with the same commits does not modify the repository.

Usage (Python 3.8+, no third party packages):

    python ocr/tools/generate_catalog.py --fetch-builtin
        Recreates a missing or damaged archive of a built-in model: downloads the
        model of the present manifests (about 35 MB), verifies its size and SHA-256
        and writes the archive. Normally it is not needed, because the archives are
        in the repository. Nothing else is generated, the GitHub API and the cache
        directory are not used.

    python ocr/tools/generate_catalog.py
        Generates the files for the commits of the present catalog.

    python ocr/tools/generate_catalog.py --fast-commit main --standard-commit main --best-commit main
        Moves the models to the newest commits. A branch, a tag and a short SHA
        are resolved to the full SHA. Check the result with "git diff", build,
        run UnitTestsOCR and commit the changed files.

    python ocr/tools/generate_catalog.py --update-builtin
        Also writes the archives of the built-in models from the cache into
        ocr/tesseract/<profile>/tessdata.

    python ocr/tools/generate_catalog.py --builtin-fast ces,eng,slk,osd --builtin-best eng
        Changes the set of the built-in models of a profile. Only the profile fast
        has built-in models, the models of the other profiles are downloaded by
        the user. An empty list removes the manifest and the license of the profile.
        The models of the installer are listed in WixInstaller/Product.wxs.in,
        update them as well.

The environment variable GITHUB_TOKEN is used for the GitHub API when it is set
(the anonymous limit of 60 requests per hour is sufficient for a normal run).
"""

import argparse
import concurrent.futures
import datetime
import hashlib
import io
import json
import lzma
import os
import shutil
import struct
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request

REPOSITORY_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CATALOG_FILE = os.path.join(REPOSITORY_ROOT, 'ocr', 'catalog', 'tesseract-catalog.json')
BUILTIN_DIRECTORY = os.path.join(REPOSITORY_ROOT, 'ocr', 'tesseract')

# Profiles in the order of the catalog and their repositories
PROFILES = ['fast', 'standard', 'best']
REPOSITORIES = {'fast': 'tessdata_fast', 'standard': 'tessdata', 'best': 'tessdata_best'}
DEFAULT_COMMIT = 'main'
MODEL_SUFFIX = '.traineddata'
ARCHIVE_SUFFIX = '.tar.xz'
MODEL_LICENSE = 'Apache-2.0'
ENGINE = 'tesseract'
ENGINE_COMPATIBILITY = 'Tesseract 4.x/5.x LSTM (tessdata_fast, tessdata, tessdata_best)'
DEFAULT_BUILTIN_LANGUAGES = {
    'fast': ['ces', 'eng', 'slk', 'deu', 'spa', 'rus', 'chi_sim', 'chi_tra', 'osd'],
    'standard': [],
    'best': [],
}

# Index of the LSTM component in the header of a traineddata file (TESSDATA_LSTM)
TESSDATA_LSTM_INDEX = 17

# Mode of a symbolic link in a git tree
GIT_SYMBOLIC_LINK_MODE = '120000'
DOWNLOAD_ATTEMPTS = 3
DOWNLOAD_THREADS = 4
USER_AGENT = 'PDF4QT-OCR-catalog-generator'

# English names of the models, the GUI of the application is in English.
# A model without a name gets its code as the name and the script prints a warning.
LANGUAGE_NAMES = {
    'afr': 'Afrikaans', 'amh': 'Amharic', 'ara': 'Arabic', 'asm': 'Assamese', 'aze': 'Azerbaijani',
    'aze_cyrl': 'Azerbaijani (Cyrillic)', 'bel': 'Belarusian', 'ben': 'Bengali', 'bod': 'Tibetan',
    'bos': 'Bosnian', 'bre': 'Breton', 'bul': 'Bulgarian', 'cat': 'Catalan', 'ceb': 'Cebuano',
    'ces': 'Czech', 'chi_sim': 'Chinese (Simplified)', 'chi_sim_vert': 'Chinese (Simplified, vertical)',
    'chi_tra': 'Chinese (Traditional)', 'chi_tra_vert': 'Chinese (Traditional, vertical)',
    'chr': 'Cherokee', 'cos': 'Corsican', 'cym': 'Welsh', 'dan': 'Danish', 'deu': 'German',
    'deu_latf': 'German (Fraktur)', 'div': 'Dhivehi', 'dzo': 'Dzongkha', 'ell': 'Greek', 'eng': 'English',
    'enm': 'English (Middle)', 'epo': 'Esperanto', 'equ': 'Math and equations', 'est': 'Estonian',
    'eus': 'Basque', 'fao': 'Faroese', 'fas': 'Persian', 'fil': 'Filipino', 'fin': 'Finnish',
    'fra': 'French', 'frk': 'Frankish', 'frm': 'French (Middle)', 'fry': 'Frisian (Western)',
    'gla': 'Scottish Gaelic', 'gle': 'Irish', 'glg': 'Galician', 'grc': 'Greek (Ancient)',
    'guj': 'Gujarati', 'hat': 'Haitian', 'heb': 'Hebrew', 'hin': 'Hindi', 'hrv': 'Croatian',
    'hun': 'Hungarian', 'hye': 'Armenian', 'iku': 'Inuktitut', 'ind': 'Indonesian', 'isl': 'Icelandic',
    'ita': 'Italian', 'ita_old': 'Italian (Old)', 'jav': 'Javanese', 'jpn': 'Japanese',
    'jpn_vert': 'Japanese (vertical)', 'kan': 'Kannada', 'kat': 'Georgian', 'kat_old': 'Georgian (Old)',
    'kaz': 'Kazakh', 'khm': 'Khmer', 'kir': 'Kyrgyz', 'kmr': 'Kurdish (Kurmanji)', 'kor': 'Korean',
    'kor_vert': 'Korean (vertical)', 'lao': 'Lao', 'lat': 'Latin', 'lav': 'Latvian', 'lit': 'Lithuanian',
    'ltz': 'Luxembourgish', 'mal': 'Malayalam', 'mar': 'Marathi', 'mkd': 'Macedonian', 'mlt': 'Maltese',
    'mon': 'Mongolian', 'mri': 'Maori', 'msa': 'Malay', 'mya': 'Burmese', 'nep': 'Nepali', 'nld': 'Dutch',
    'nor': 'Norwegian', 'oci': 'Occitan', 'ori': 'Oriya', 'osd': 'Orientation and script detection',
    'pan': 'Punjabi', 'pol': 'Polish', 'por': 'Portuguese', 'pus': 'Pashto', 'que': 'Quechua',
    'ron': 'Romanian', 'rus': 'Russian', 'san': 'Sanskrit', 'sin': 'Sinhala', 'slk': 'Slovak',
    'slv': 'Slovenian', 'snd': 'Sindhi', 'spa': 'Spanish', 'spa_old': 'Spanish (Old)', 'sqi': 'Albanian',
    'srp': 'Serbian', 'srp_latn': 'Serbian (Latin)', 'sun': 'Sundanese', 'swa': 'Swahili',
    'swe': 'Swedish', 'syr': 'Syriac', 'tam': 'Tamil', 'tat': 'Tatar', 'tel': 'Telugu', 'tgk': 'Tajik',
    'tgl': 'Tagalog', 'dan_frak': 'Danish (Fraktur)', 'deu_frak': 'German (Fraktur, legacy)', 'slk_frak': 'Slovak (Fraktur)',
    'tha': 'Thai', 'tir': 'Tigrinya', 'ton': 'Tongan', 'tur': 'Turkish', 'uig': 'Uyghur',
    'ukr': 'Ukrainian', 'urd': 'Urdu', 'uzb': 'Uzbek', 'uzb_cyrl': 'Uzbek (Cyrillic)',
    'vie': 'Vietnamese', 'yid': 'Yiddish', 'yor': 'Yoruba',
}


class GeneratorError(Exception):
    pass


def warning(text):
    sys.stdout.flush()
    print('WARNING: ' + text, file=sys.stderr)


def repository_name(profile):
    return REPOSITORIES[profile]


def builtin_path(profile, *names):
    return os.path.join(BUILTIN_DIRECTORY, profile, *names)


def repository_url(profile):
    return 'https://github.com/tesseract-ocr/' + repository_name(profile)


def raw_url(profile, commit, path):
    return 'https://raw.githubusercontent.com/tesseract-ocr/{}/{}/{}'.format(repository_name(profile), commit, path)


def open_url(url):
    headers = {'User-Agent': USER_AGENT}
    token = os.environ.get('GITHUB_TOKEN')
    if token and url.startswith('https://api.github.com/'):
        headers['Authorization'] = 'Bearer ' + token
        headers['Accept'] = 'application/vnd.github+json'
    return urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=60)


def read_api(profile, path):
    url = 'https://api.github.com/repos/tesseract-ocr/{}/{}'.format(repository_name(profile), path)
    try:
        with open_url(url) as response:
            return json.loads(response.read().decode('utf-8'))
    except urllib.error.HTTPError as error:
        raise GeneratorError('GitHub API request {} failed: {} {}'.format(url, error.code, error.reason))
    except (urllib.error.URLError, OSError) as error:
        raise GeneratorError('GitHub API request {} failed: {}'.format(url, error))


def read_json_file(file_name):
    if not os.path.isfile(file_name):
        return None
    with open(file_name, 'r', encoding='utf-8') as file:
        return json.load(file)


def write_text_file(file_name, text):
    # Text files of the repository have CRLF line endings on every platform
    data = text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8')
    if os.path.isfile(file_name):
        with open(file_name, 'rb') as file:
            if file.read() == data:
                return False
    os.makedirs(os.path.dirname(file_name), exist_ok=True)
    with open(file_name, 'wb') as file:
        file.write(data)
    return True


def write_json_file(file_name, value):
    return write_text_file(file_name, json.dumps(value, indent=1, ensure_ascii=False))


def read_archive_model(file_name):
    """Returns the SHA-256 of the single model of the archive, or None when it cannot be read."""
    try:
        with tarfile.open(file_name, 'r:xz') as archive:
            members = [member for member in archive.getmembers() if member.isfile()]
            if len(members) != 1:
                return None
            return hashlib.sha256(archive.extractfile(members[0]).read()).hexdigest()
    except (OSError, tarfile.TarError, lzma.LZMAError, EOFError):
        return None


def write_model_archive(file_name, model_file_name, data):
    """Writes the model into a deterministic tar.xz archive (the same input gives the same bytes)."""
    info = tarfile.TarInfo(model_file_name)
    info.size = len(data)
    info.mtime = 0
    info.mode = 0o644
    info.uid = 0
    info.gid = 0
    info.uname = ''
    info.gname = ''

    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w', format=tarfile.USTAR_FORMAT) as archive:
        archive.addfile(info, io.BytesIO(data))

    os.makedirs(os.path.dirname(file_name), exist_ok=True)
    with open(file_name, 'wb') as file:
        file.write(lzma.compress(buffer.getvalue(), preset=9 | lzma.PRESET_EXTREME))


def compute_hashes(file_name):
    """Returns the size, the git blob SHA-1 and the SHA-256 of the file."""
    size = os.path.getsize(file_name)
    sha1 = hashlib.sha1('blob {}\0'.format(size).encode('ascii'))
    sha256 = hashlib.sha256()
    with open(file_name, 'rb') as file:
        while True:
            block = file.read(1 << 20)
            if not block:
                break
            sha1.update(block)
            sha256.update(block)
    return size, sha1.hexdigest(), sha256.hexdigest()


def has_lstm_component(file_name):
    """Returns true, if the traineddata file contains a model of the LSTM engine."""
    with open(file_name, 'rb') as file:
        header = file.read(4)
        if len(header) != 4:
            return False
        count = struct.unpack('<i', header)[0]
        if count <= TESSDATA_LSTM_INDEX or count > 1024:
            return False
        offsets = file.read(8 * count)
        if len(offsets) != 8 * count:
            return False
        return struct.unpack('<{}q'.format(count), offsets)[TESSDATA_LSTM_INDEX] >= 0


def resolve_commit(profile, reference):
    commit = read_api(profile, 'commits/' + reference).get('sha', '')
    if len(commit) != 40:
        raise GeneratorError('Commit "{}" of {} was not found.'.format(reference, repository_name(profile)))
    return commit


def read_tree(profile, commit):
    tree = read_api(profile, 'git/trees/{}?recursive=1'.format(commit))
    if tree.get('truncated'):
        raise GeneratorError('List of the files of {} is truncated.'.format(repository_name(profile)))
    return {item['path']: item for item in tree.get('tree', []) if item.get('type') == 'blob'}


def fetch_file(profile, commit, item, cache_directory):
    """Returns the file of the cache, which is verified against the commit, and its SHA-256."""
    path = item['path']
    file_name = os.path.join(cache_directory, profile, *path.split('/'))

    def verify(name):
        size, sha1, sha256 = compute_hashes(name)
        return sha256 if size == item['size'] and sha1 == item['sha'] else None

    if os.path.isfile(file_name):
        sha256 = verify(file_name)
        if sha256:
            return file_name, sha256, False

    os.makedirs(os.path.dirname(file_name), exist_ok=True)
    part_file_name = file_name + '.part'
    url = raw_url(profile, commit, path)
    last_error = None
    for attempt in range(DOWNLOAD_ATTEMPTS):
        try:
            with open_url(url) as response, open(part_file_name, 'wb') as file:
                shutil.copyfileobj(response, file, 1 << 20)
            sha256 = verify(part_file_name)
            if sha256:
                os.replace(part_file_name, file_name)
                return file_name, sha256, True
            last_error = 'size or git SHA-1 does not match the commit'
        except (urllib.error.URLError, OSError) as error:
            last_error = str(error)
        time.sleep(2 * (attempt + 1))

    if os.path.isfile(part_file_name):
        os.remove(part_file_name)
    raise GeneratorError('Download of {} failed: {}'.format(url, last_error))


def create_model(profile, commit, item, sha256):
    path = item['path']
    code = path[:-len(MODEL_SUFFIX)]
    language = code.split('/')[-1]
    is_script = code.startswith('script/')

    if is_script:
        name = 'Script: ' + language.replace('_', ' ')
        family = 'script'
    else:
        if language not in LANGUAGE_NAMES:
            warning('model "{}" has no name in LANGUAGE_NAMES, its code is used.'.format(language))
        name = LANGUAGE_NAMES.get(language, language)
        family = 'osd' if language == 'osd' else 'language'

    return {
        'id': '{}/{}/{}'.format(ENGINE, profile, code),
        'engine': ENGINE,
        'language': code,
        'name': name,
        'profile': profile,
        'family': family,
        'version': commit,
        'url': raw_url(profile, commit, path),
        'fileName': path,
        'size': item['size'],
        'sha256': sha256,
        'license': MODEL_LICENSE,
        'dependencies': [],
    }


def create_builtin_model(model):
    return {
        'id': model['id'],
        'language': model['language'],
        'name': model['name'],
        'profile': model['profile'],
        'fileName': model['fileName'],
        'size': model['size'],
        'sha256': model['sha256'],
        'version': model['version'],
        'license': model['license'],
        'source': repository_url(model['profile']),
    }


def synchronize_builtin_models(profile, builtin_models, cached_files, update, changed_files):
    tessdata_directory = builtin_path(profile, 'tessdata')
    relative_directory = 'ocr/tesseract/{}/tessdata'.format(profile)
    expected_files = set()
    for model in builtin_models:
        archive_name = model['language'] + ARCHIVE_SUFFIX
        expected_files.add(archive_name)
        file_name = os.path.join(tessdata_directory, archive_name)
        if read_archive_model(file_name) == model['sha256']:
            continue
        if update:
            with open(cached_files[model['id']], 'rb') as file:
                write_model_archive(file_name, model['fileName'], file.read())
            changed_files.append(file_name)
        else:
            state = 'does not hold the model of the manifest' if os.path.isfile(file_name) else 'is missing'
            warning('archive {}/{} {}, run the script with --fetch-builtin.'.format(relative_directory, archive_name, state))

    if os.path.isdir(tessdata_directory):
        for name in sorted(os.listdir(tessdata_directory)):
            if name.endswith(ARCHIVE_SUFFIX) and name not in expected_files:
                os.remove(os.path.join(tessdata_directory, name))
                changed_files.append(os.path.join(tessdata_directory, name))
            elif name.endswith(MODEL_SUFFIX):
                # Extracted model in the source tree, the build tree is filled by CMake
                warning('{}/{} is not stored in the repository, it can be removed.'.format(relative_directory, name))


def download_verified_model(url, size, sha256):
    """Downloads the model and verifies it against the size and SHA-256 of the manifest."""
    last_error = None
    for attempt in range(DOWNLOAD_ATTEMPTS):
        try:
            with open_url(url) as response:
                data = response.read()
            if len(data) == size and hashlib.sha256(data).hexdigest() == sha256:
                return data
            last_error = 'size or SHA-256 does not match the manifest'
        except (urllib.error.URLError, OSError) as error:
            last_error = str(error)
        time.sleep(2 * (attempt + 1))

    raise GeneratorError('Download of {} failed: {}'.format(url, last_error))


def fetch_builtin_models():
    """Recreates the missing archives of the built-in models, nothing else is generated."""
    catalog = read_json_file(CATALOG_FILE)
    if not catalog:
        raise GeneratorError('Catalog {} was not found.'.format(CATALOG_FILE))
    urls = {model['id']: model['url'] for model in catalog.get('models', [])}

    downloaded_count = 0
    valid_count = 0
    for profile in PROFILES:
        manifest = read_json_file(builtin_path(profile, 'manifest.json')) or {}
        for model in manifest.get('models', []):
            file_name = builtin_path(profile, 'tessdata', model['language'] + ARCHIVE_SUFFIX)
            relative_name = 'ocr/tesseract/{}/tessdata/{}{}'.format(profile, model['language'], ARCHIVE_SUFFIX)
            if read_archive_model(file_name) == model['sha256']:
                valid_count += 1
                continue
            if model['id'] not in urls:
                raise GeneratorError('Built-in model {} is not in the catalog.'.format(model['id']))
            print('Downloading {} ({:.1f} MB)'.format(model['fileName'], model['size'] / 1e6))
            write_model_archive(file_name, model['fileName'], download_verified_model(urls[model['id']], model['size'], model['sha256']))
            print('Written {}'.format(relative_name))
            downloaded_count += 1

    print('{} archives written, {} already valid.'.format(downloaded_count, valid_count))
    return 0


def main():
    parser = argparse.ArgumentParser(description='Generates the catalog of the OCR language models of Tesseract.')
    parser.add_argument('--fetch-builtin', action='store_true',
                        help='only download the built-in models of the present manifests and verify them')
    for profile in PROFILES:
        parser.add_argument('--{}-commit'.format(profile), metavar='REF',
                            help='commit, branch or tag of {} (default: commit of the present catalog)'.format(repository_name(profile)))
    for profile in PROFILES:
        parser.add_argument('--builtin-{}'.format(profile), metavar='LANGUAGES',
                            help='comma separated built-in models of the profile {} (default: models of the present manifest)'.format(profile))
    parser.add_argument('--update-builtin', action='store_true',
                        help='write the archives of the built-in models into ocr/tesseract/<profile>/tessdata')
    parser.add_argument('--cache-dir', metavar='DIRECTORY', default=os.path.join(tempfile.gettempdir(), 'pdf4qt-ocr-catalog'),
                        help='directory of the downloaded models (default: %(default)s)')
    arguments = parser.parse_args()

    if arguments.fetch_builtin:
        return fetch_builtin_models()

    old_catalog = read_json_file(CATALOG_FILE) or {}

    builtin_languages = {}
    for profile in PROFILES:
        argument = getattr(arguments, 'builtin_' + profile)
        old_manifest = read_json_file(builtin_path(profile, 'manifest.json')) or {}
        if argument is not None:
            builtin_languages[profile] = [language.strip() for language in argument.split(',') if language.strip()]
        elif old_manifest.get('models'):
            builtin_languages[profile] = [model['language'] for model in old_manifest['models']]
        else:
            builtin_languages[profile] = DEFAULT_BUILTIN_LANGUAGES[profile]

    commits = {}
    trees = {}
    for profile in PROFILES:
        reference = getattr(arguments, profile + '_commit') or old_catalog.get('sources', {}).get(profile, {}).get('commit')
        if not reference:
            print('{}: commit is not in the catalog, {} is used.'.format(repository_name(profile), DEFAULT_COMMIT))
            reference = DEFAULT_COMMIT
        commits[profile] = resolve_commit(profile, reference)
        trees[profile] = read_tree(profile, commits[profile])
        print('{}: commit {}'.format(repository_name(profile), commits[profile]))

    # Download and verification of the models
    tasks = []
    for profile in PROFILES:
        for path, item in sorted(trees[profile].items()):
            if not path.endswith(MODEL_SUFFIX):
                continue
            if item.get('mode') == GIT_SYMBOLIC_LINK_MODE:
                print('{}/{} skipped, symbolic link'.format(repository_name(profile), path))
                continue
            tasks.append((profile, item))
    print('Verifying {} models in {}'.format(len(tasks), arguments.cache_dir))

    models = []
    cached_files = {}
    downloaded_count = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=DOWNLOAD_THREADS) as executor:
        futures = [executor.submit(fetch_file, profile, commits[profile], item, arguments.cache_dir) for profile, item in tasks]
        for index, ((profile, item), future) in enumerate(zip(tasks, futures)):
            file_name, sha256, is_downloaded = future.result()
            downloaded_count += 1 if is_downloaded else 0
            model = create_model(profile, commits[profile], item, sha256)
            state = ' (downloaded)' if is_downloaded else ''
            if model['family'] != 'osd' and not has_lstm_component(file_name):
                print('[{}/{}] {}{} skipped, no LSTM model'.format(index + 1, len(tasks), model['id'], state))
                continue
            models.append(model)
            cached_files[model['id']] = file_name
            print('[{}/{}] {}{}'.format(index + 1, len(tasks), model['id'], state))

    # Catalog
    catalog = {
        'format': 'pdf4qt-ocr-catalog',
        'version': 1,
        'generated': old_catalog.get('generated', ''),
        'engine': ENGINE,
        'engineCompatibility': ENGINE_COMPATIBILITY,
        'sources': {profile: {'repository': repository_url(profile), 'commit': commits[profile], 'license': MODEL_LICENSE}
                    for profile in PROFILES},
        'models': models,
    }
    if catalog != old_catalog:
        catalog['generated'] = datetime.date.today().isoformat()

    changed_files = []
    if write_json_file(CATALOG_FILE, catalog):
        changed_files.append(CATALOG_FILE)

    # Manifests and licenses of the built-in models
    models_by_id = {model['id']: model for model in models}
    builtin_count = 0
    for profile in PROFILES:
        if not builtin_languages[profile]:
            # Profile without built-in models has no manifest and no license
            for name in ['manifest.json', 'LICENSE']:
                if os.path.isfile(builtin_path(profile, name)):
                    os.remove(builtin_path(profile, name))
                    changed_files.append(builtin_path(profile, name))
            synchronize_builtin_models(profile, [], cached_files, True, changed_files)
            continue

        builtin_models = []
        for language in builtin_languages[profile]:
            model = models_by_id.get('{}/{}/{}'.format(ENGINE, profile, language))
            if model is None:
                raise GeneratorError('Built-in model "{}" is not in {}.'.format(language, repository_name(profile)))
            builtin_models.append(create_builtin_model(model))
        builtin_count += len(builtin_models)

        manifest = {
            'format': 'pdf4qt-ocr-builtin-manifest',
            'version': 1,
            'engine': ENGINE,
            'profile': profile,
            'setId': 'tessdata_{}-{}'.format(profile, commits[profile][:12]),
            'license': MODEL_LICENSE,
            'licenseFile': 'LICENSE',
            'models': builtin_models,
        }

        license_item = trees[profile].get('LICENSE')
        if license_item is None:
            raise GeneratorError('File LICENSE is not in {}.'.format(repository_name(profile)))
        license_file_name = fetch_file(profile, commits[profile], license_item, arguments.cache_dir)[0]
        with open(license_file_name, 'r', encoding='utf-8') as file:
            license_text = file.read()

        if write_json_file(builtin_path(profile, 'manifest.json'), manifest):
            changed_files.append(builtin_path(profile, 'manifest.json'))
        if write_text_file(builtin_path(profile, 'LICENSE'), license_text):
            changed_files.append(builtin_path(profile, 'LICENSE'))

        synchronize_builtin_models(profile, builtin_models, cached_files, arguments.update_builtin, changed_files)

    print('{} models, {} downloaded, {} built-in.'.format(len(models), downloaded_count, builtin_count))
    for file_name in changed_files:
        print('Changed: ' + os.path.relpath(file_name, REPOSITORY_ROOT))
    if not changed_files:
        print('Generated files are up to date.')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except GeneratorError as error:
        sys.stdout.flush()
        print('ERROR: {}'.format(error), file=sys.stderr)
        sys.exit(1)
