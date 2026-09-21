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

    ocr/catalog/tesseract-catalog.json   all models of tessdata_fast and tessdata_best
                                         (compiled into Pdf4QtLibCore by ocr.qrc)
    ocr/tesseract/fast/manifest.json     models distributed with the application
    ocr/tesseract/fast/LICENSE           license of tessdata_fast

Both model repositories are pinned to a commit. The list of the files of the commit
is read from the GitHub API, every *.traineddata file is downloaded from
raw.githubusercontent.com, verified against the size and the git blob SHA-1 of the
commit, and its SHA-256 is stored in the catalog. The application never downloads
a catalog from the network, so the checksums can be changed only by this script.

The downloads are kept in a cache directory (about 1.5 GB for both profiles), a file
of the cache is downloaded again only when it does not match the commit. The field
"generated" of the catalog changes only when something else has changed, so
a repeated run with the same commits does not modify the repository.

Usage (Python 3.8+, no third party packages):

    python ocr/tools/generate_catalog.py
        Generates the files for the commits of the present catalog.

    python ocr/tools/generate_catalog.py --fast-commit main --best-commit main
        Moves the models to the newest commits. A branch, a tag and a short SHA
        are resolved to the full SHA. Check the result with "git diff", build,
        run UnitTestsOCR and commit the three files.

    python ocr/tools/generate_catalog.py --update-builtin
        Also copies the built-in models into ocr/tesseract/fast/tessdata
        (the model files are not stored in the git repository).

    python ocr/tools/generate_catalog.py --builtin ces,eng,slk,osd
        Changes the set of the built-in models.

The environment variable GITHUB_TOKEN is used for the GitHub API when it is set
(the anonymous limit of 60 requests per hour is sufficient for a normal run).
"""

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import urllib.error
import urllib.request

REPOSITORY_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CATALOG_FILE = os.path.join(REPOSITORY_ROOT, 'ocr', 'catalog', 'tesseract-catalog.json')
BUILTIN_DIRECTORY = os.path.join(REPOSITORY_ROOT, 'ocr', 'tesseract', 'fast')
MANIFEST_FILE = os.path.join(BUILTIN_DIRECTORY, 'manifest.json')
LICENSE_FILE = os.path.join(BUILTIN_DIRECTORY, 'LICENSE')
TESSDATA_DIRECTORY = os.path.join(BUILTIN_DIRECTORY, 'tessdata')

PROFILES = ['fast', 'best']
BUILTIN_PROFILE = 'fast'
MODEL_SUFFIX = '.traineddata'
MODEL_LICENSE = 'Apache-2.0'
ENGINE = 'tesseract'
ENGINE_COMPATIBILITY = 'Tesseract 4.x/5.x LSTM (tessdata_fast, tessdata_best)'
DEFAULT_BUILTIN_LANGUAGES = ['ces', 'eng', 'slk', 'deu', 'spa', 'rus', 'chi_sim', 'chi_tra', 'osd']
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
    return 'tessdata_' + profile


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


def synchronize_builtin_models(builtin_models, cached_files, update):
    expected_files = set()
    for model in builtin_models:
        expected_files.add(model['fileName'])
        file_name = os.path.join(TESSDATA_DIRECTORY, model['fileName'])
        is_valid = os.path.isfile(file_name) and compute_hashes(file_name)[2] == model['sha256']
        if is_valid:
            continue
        if update:
            os.makedirs(TESSDATA_DIRECTORY, exist_ok=True)
            shutil.copyfile(cached_files[model['id']], file_name)
            print('Built-in model {} updated.'.format(model['fileName']))
        else:
            state = 'differs from the manifest' if os.path.isfile(file_name) else 'is missing'
            warning('built-in model {} {}, run the script with --update-builtin.'.format(model['fileName'], state))

    if os.path.isdir(TESSDATA_DIRECTORY):
        for name in sorted(os.listdir(TESSDATA_DIRECTORY)):
            if name.endswith(MODEL_SUFFIX) and name not in expected_files:
                warning('{} in ocr/tesseract/fast/tessdata is not a built-in model, remove it.'.format(name))


def main():
    parser = argparse.ArgumentParser(description='Generates the catalog of the OCR language models of Tesseract.')
    for profile in PROFILES:
        parser.add_argument('--{}-commit'.format(profile), metavar='REF',
                            help='commit, branch or tag of {} (default: commit of the present catalog)'.format(repository_name(profile)))
    parser.add_argument('--builtin', metavar='LANGUAGES',
                        help='comma separated built-in models (default: models of the present manifest)')
    parser.add_argument('--update-builtin', action='store_true',
                        help='copy the built-in models into ocr/tesseract/fast/tessdata')
    parser.add_argument('--cache-dir', metavar='DIRECTORY', default=os.path.join(tempfile.gettempdir(), 'pdf4qt-ocr-catalog'),
                        help='directory of the downloaded models (default: %(default)s)')
    arguments = parser.parse_args()

    old_catalog = read_json_file(CATALOG_FILE) or {}
    old_manifest = read_json_file(MANIFEST_FILE) or {}

    if arguments.builtin:
        builtin_languages = [language.strip() for language in arguments.builtin.split(',') if language.strip()]
    elif old_manifest.get('models'):
        builtin_languages = [model['language'] for model in old_manifest['models']]
    else:
        builtin_languages = DEFAULT_BUILTIN_LANGUAGES

    commits = {}
    trees = {}
    for profile in PROFILES:
        reference = getattr(arguments, profile + '_commit') or old_catalog.get('sources', {}).get(profile, {}).get('commit')
        if not reference:
            raise GeneratorError('Commit of {} is not known, use --{}-commit.'.format(repository_name(profile), profile))
        commits[profile] = resolve_commit(profile, reference)
        trees[profile] = read_tree(profile, commits[profile])
        print('{}: commit {}'.format(repository_name(profile), commits[profile]))

    # Download and verification of the models
    tasks = [(profile, item) for profile in PROFILES
             for path, item in sorted(trees[profile].items()) if path.endswith(MODEL_SUFFIX)]
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
            models.append(model)
            cached_files[model['id']] = file_name
            print('[{}/{}] {}'.format(index + 1, len(tasks), model['id']) + (' (downloaded)' if is_downloaded else ''))

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

    # Manifest of the built-in models
    models_by_id = {model['id']: model for model in models}
    builtin_models = []
    for language in builtin_languages:
        model = models_by_id.get('{}/{}/{}'.format(ENGINE, BUILTIN_PROFILE, language))
        if model is None:
            raise GeneratorError('Built-in model "{}" is not in {}.'.format(language, repository_name(BUILTIN_PROFILE)))
        builtin_models.append(create_builtin_model(model))

    manifest = {
        'format': 'pdf4qt-ocr-builtin-manifest',
        'version': 1,
        'engine': ENGINE,
        'profile': BUILTIN_PROFILE,
        'setId': '{}-{}'.format(repository_name(BUILTIN_PROFILE), commits[BUILTIN_PROFILE][:12]),
        'license': MODEL_LICENSE,
        'licenseFile': 'LICENSE',
        'models': builtin_models,
    }

    # License of the built-in models
    license_item = trees[BUILTIN_PROFILE].get('LICENSE')
    if license_item is None:
        raise GeneratorError('File LICENSE is not in {}.'.format(repository_name(BUILTIN_PROFILE)))
    license_file_name = fetch_file(BUILTIN_PROFILE, commits[BUILTIN_PROFILE], license_item, arguments.cache_dir)[0]
    with open(license_file_name, 'r', encoding='utf-8') as file:
        license_text = file.read()

    changed_files = []
    if write_json_file(CATALOG_FILE, catalog):
        changed_files.append(CATALOG_FILE)
    if write_json_file(MANIFEST_FILE, manifest):
        changed_files.append(MANIFEST_FILE)
    if write_text_file(LICENSE_FILE, license_text):
        changed_files.append(LICENSE_FILE)

    synchronize_builtin_models(builtin_models, cached_files, arguments.update_builtin)

    print('{} models, {} downloaded, {} built-in.'.format(len(models), downloaded_count, len(builtin_models)))
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
