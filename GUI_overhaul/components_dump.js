// Read-only dump of the components of the Figma page Components for GUI_overhaul/components_spec.md.
// Run it by the tool use_figma (file key QVGmx58DYmMq41kwfpMjtl); it reads only, it changes nothing in Figma.
// Fill IDS with the ids of the components or of the component sets. The answer is { next, out }: when next is
// [ci, bi], run the script again with CI0 = ci and BI0 = bi (the answer of the tool is limited to 20 kB).
const IDS = ['48:170'], CI0 = 0, BI0 = 0, LIMIT = 18500;
figma.skipInvisibleInstanceChildren = false;
await figma.setCurrentPageAsync(figma.root.children.find(p => p.id === '158:4605'));
const vc = {}, va = {}, nc = {}, r = x => Math.round(x * 100) / 100, M = figma.mixed;
const hx = c => '#' + [c.r, c.g, c.b].map(v => Math.round(v * 255).toString(16).padStart(2, '0')).join('');
async function vn(id, n) { if (!(id in vc)) { const v = await figma.variables.getVariableByIdAsync(id); vc[id] = v ? v.name : '?' + id; try { va[id] = r(v.resolveForConsumer(n).value.a); } catch (e) { va[id] = 1; } } return vc[id]; }
async function pt(p, n) {
  if (!p || p.visible === false) return '';
  if (p.type !== 'SOLID') return p.type + (p.gradientStops ? '(' + p.gradientStops.map(g => hx(g.color) + '@' + r(g.color.a) + ':' + r(g.position)).join(',') + ')' : '');
  const b = p.boundVariables && p.boundVariables.color, o = p.opacity === undefined ? 1 : r(p.opacity);
  if (b) { const name = await vn(b.id, n); return name + (o !== va[b.id] ? '@' + o : ''); }
  return hx(p.color) + '!' + (o !== 1 ? '@' + o : '');
}
async function pts(a, n) { if (a === M) return 'mixed'; const o = []; for (const p of (a || [])) { const s = await pt(p, n); if (s) o.push(s); } return o.join('+'); }
async function nm(id) { if (!(id in nc)) { const x = await figma.getNodeByIdAsync(id); nc[id] = x ? x.name : '?' + id; } return nc[id]; }
const SZ = { FIXED: 'fix', HUG: 'hug', FILL: 'fill' }, TY = { FRAME: 'F', TEXT: 'T', INSTANCE: 'I', RECTANGLE: 'R', ELLIPSE: 'E', VECTOR: 'V', GROUP: 'G', COMPONENT: 'C', LINE: 'L', BOOLEAN_OPERATION: 'B' };
const q = s => "'" + String(s).replace(/\n/g, '\\n').replace(/"/g, "''") + "'";
async function attrs(n, pl) {
  const a = { t: TY[n.type] || n.type, n: n.name, s: r(n.width) + 'x' + r(n.height) };
  if (!n.visible) a.hid = 1;
  if (pl === 'NONE' || n.layoutPositioning === 'ABSOLUTE') { a.at = r(n.x) + ',' + r(n.y) + (n.layoutPositioning === 'ABSOLUTE' ? ' abs' : ''); if ('constraints' in n && (n.constraints.horizontal !== 'MIN' || n.constraints.vertical !== 'MIN')) a.con = n.constraints.horizontal + '/' + n.constraints.vertical; }
  if ('rotation' in n && r(n.rotation) !== 0) a.rot = r(n.rotation);
  if ('layoutMode' in n && n.layoutMode !== 'NONE') a.lay = n.layoutMode[0] + ' gap' + r(n.itemSpacing) + ' pad' + [n.paddingTop, n.paddingRight, n.paddingBottom, n.paddingLeft].map(r).join(',') + ' ' + n.primaryAxisAlignItems + '/' + n.counterAxisAlignItems + (n.layoutWrap === 'WRAP' ? ' wrap' + r(n.counterAxisSpacing) : '');
  if ('layoutSizingHorizontal' in n && pl !== undefined) a.z = SZ[n.layoutSizingHorizontal] + '/' + SZ[n.layoutSizingVertical];
  else if (pl === undefined) { const m = x => x === 'AUTO' ? 'hug' : 'fix', h = n.layoutMode === 'HORIZONTAL'; a.z = !n.layoutMode || n.layoutMode === 'NONE' ? 'fix/fix' : m(h ? n.primaryAxisSizingMode : n.counterAxisSizingMode) + '/' + m(h ? n.counterAxisSizingMode : n.primaryAxisSizingMode); }
  if ('strokesIncludedInLayout' in n && n.layoutMode && n.layoutMode !== 'NONE' && n.strokes.length && !n.strokesIncludedInLayout) a.sil = 0;
  if ('minWidth' in n) { const m = [n.minWidth, n.maxWidth, n.minHeight, n.maxHeight]; if (m.some(v => v)) a.mm = m.map(v => v ? r(v) : '-').join(','); }
  if ('fills' in n) { const f = await pts(n.fills, n); if (f) a.fill = f; }
  if ('strokes' in n && n.strokes.length) { const s = await pts(n.strokes, n); if (s) a.st = s + ' w' + (n.strokeWeight === M ? [n.strokeTopWeight, n.strokeRightWeight, n.strokeBottomWeight, n.strokeLeftWeight].map(r).join(',') : r(n.strokeWeight)) + ' ' + n.strokeAlign + (n.dashPattern && n.dashPattern.length ? ' dash' + n.dashPattern.join(',') : ''); }
  if ('cornerRadius' in n) { const c = n.cornerRadius === M ? [n.topLeftRadius, n.topRightRadius, n.bottomRightRadius, n.bottomLeftRadius].map(r).join(',') : r(n.cornerRadius); if (c !== 0) a.rad = c; }
  if ('effects' in n && n.effects.length) { const e = []; for (const f of n.effects) { if (f.visible === false) continue; if (f.color) { const b = f.boundVariables && f.boundVariables.color; e.push(f.type + '(' + [f.offset.x, f.offset.y, f.radius, f.spread || 0].map(r).join(',') + ' ' + (b ? await vn(b.id, n) : hx(f.color) + '!@' + r(f.color.a)) + ')'); } else e.push(f.type + '(' + r(f.radius) + ')'); } if (e.length) a.fx = e.join('+'); }
  if ('opacity' in n && r(n.opacity) !== 1) a.op = r(n.opacity);
  if ('clipsContent' in n && n.clipsContent) a.clip = 1;
  if (n.type === 'TEXT') {
    const lh = n.lineHeight === M ? 'mixed' : (n.lineHeight.unit === 'AUTO' ? 'auto' : r(n.lineHeight.value) + (n.lineHeight.unit === 'PERCENT' ? '%' : ''));
    const ls = n.letterSpacing === M ? ' ls-mixed' : (r(n.letterSpacing.value) === 0 ? '' : ' ls' + r(n.letterSpacing.value) + (n.letterSpacing.unit === 'PERCENT' ? '%' : 'px'));
    a.f = (n.fontName === M ? 'mixed' : n.fontName.family + ' ' + n.fontName.style) + ' ' + (n.fontSize === M ? 'mixed' : n.fontSize) + '/' + lh + ls + ' ' + n.textAlignHorizontal[0] + n.textAlignVertical[0] + ' ' + n.textAutoResize + (n.textTruncation === 'ENDING' ? ' trunc' + (n.maxLines || '') : '') + (n.textDecoration !== M && n.textDecoration !== 'NONE' ? ' ' + n.textDecoration : '') + (n.textCase !== M && n.textCase !== 'ORIGINAL' ? ' ' + n.textCase : '');
    a.tx = q(n.characters.length > 44 ? n.characters.slice(0, 42) + '…' : n.characters);
  }
  if (n.componentPropertyReferences) { const k = Object.keys(n.componentPropertyReferences); if (k.length) a.ref = k.map(x => x + ':' + n.componentPropertyReferences[x].replace(/#.*$/, '')).join(','); }
  if (n.type === 'INSTANCE') {
    const mc = await n.getMainComponentAsync();
    const mn = mc ? (mc.parent && mc.parent.type === 'COMPONENT_SET' ? mc.parent.name + '{' + mc.name + '}' : mc.name) : '?';
    if (mn.startsWith('icon/') || mn.startsWith('Logo')) {
      const d = n.findOne(x => ('strokes' in x && x.strokes.length > 0) || ('fills' in x && x.fills !== M && x.fills.length > 0 && x.type !== 'FRAME' && x.type !== 'INSTANCE'));
      const fs = d && d.fills !== M && d.fills.length ? await pts(d.fills, d) : '';
      a.ic = mn + (d ? (d.strokes.length ? ' stroke=' + await pts(d.strokes, d) + ' w' + (d.strokeWeight === M ? 'mixed' : r(d.strokeWeight)) : '') + (fs ? ' fill=' + fs : '') : ' (no paint found)');
    } else {
      const cp = n.componentProperties, o = [], ov = {};
      for (const k of Object.keys(cp)) { if (cp[k].type === 'VARIANT') continue; o.push(k.replace(/#.*$/, '') + '=' + (cp[k].type === 'INSTANCE_SWAP' ? await nm(cp[k].value) : cp[k].type === 'TEXT' ? q(String(cp[k].value).slice(0, 30)) : cp[k].value)); }
      for (const x of (n.overrides || [])) for (const f of x.overriddenFields) ov[f] = 1;
      a.inst = mn + (o.length ? ' [' + o.join(', ') + ']' : '') + (Object.keys(ov).length ? ' ov:' + Object.keys(ov).join(',') : '');
      if (n.isExposedInstance) a.exp = 1;
    }
  }
  return a;
}
async function walk(n, pl, path, out, depth) {
  out.push({ path, depth, a: await attrs(n, pl) });
  if ('children' in n && n.type !== 'INSTANCE') {
    const cnt = {}, seen = {}; for (const c of n.children) cnt[c.name] = (cnt[c.name] || 0) + 1;
    for (const c of n.children) { let k = c.name; if (cnt[k] > 1) { seen[k] = (seen[k] || 0) + 1; k += '#' + seen[k]; } await walk(c, n.layoutMode || 'NONE', path ? path + '>' + k : k, out, depth + 1); }
  }
}
const fmt = a => Object.keys(a).filter(k => k !== 't' && k !== 'n' && k !== 's').map(k => k + '=' + a[k]).join('; ');
const vm = s => { const o = {}; for (const p of s.split(', ')) { const i = p.indexOf('='); o[p.slice(0, i)] = p.slice(i + 1); } return o; };
async function blocks(id) {
  const n = await figma.getNodeByIdAsync(id), B = [];
  if (!n) return ['## ' + id + ' NOT FOUND'];
  const MAXN = /preview|Sample page|Logo/.test(n.name) ? 14 : 80;
  let d = {}; try { d = n.componentPropertyDefinitions; } catch (e) {}
  const props = []; for (const k of Object.keys(d)) { const v = d[k]; props.push(k.replace(/#.*$/, '') + ':' + v.type + (v.variantOptions ? '[' + v.variantOptions.join(',') + ']' : '') + '=' + (v.type === 'INSTANCE_SWAP' ? await nm(v.defaultValue) : v.defaultValue)); }
  const isSet = n.type === 'COMPONENT_SET', dv = isSet ? n.defaultVariant : n, W = {};
  const tree = async v => { if (!W[v.id]) { W[v.id] = []; await walk(v, undefined, '', W[v.id], 0); } return W[v.id]; };
  const base = await tree(dv);
  B.push('## ' + n.id + ' ' + n.name + (isSet ? ' SET ' + n.children.length + ' variants' : ' COMPONENT') + '\nprops: ' + (props.join(' | ') || '-') + (n.description ? '\ndesc: ' + n.description.replace(/\n/g, ' ') : '') + '\n# default' + (isSet ? ': ' + dv.name : '') + '\n' + base.slice(0, MAXN).map(x => ' '.repeat(x.depth) + x.a.t + ' ' + q(x.a.n) + ' ' + x.a.s + ' ' + fmt(x.a)).join('\n') + (base.length > MAXN ? '\n… +' + (base.length - MAXN) + ' nodes' : ''));
  if (isSet) {
    const dm = vm(dv.name);
    for (const v of n.children) {
      if (v.id === dv.id) continue;
      let bv = dv; const m = vm(v.name);
      if ('State' in m && m.State !== dm.State) { const want = Object.keys(m).map(k => k + '=' + (k === 'State' ? dm.State : m[k])).join(', '); const c = n.children.find(x => x.name === want); if (c) bv = c; }
      const bt = await tree(bv), cur = await tree(v), bm = {}, cm = {}, L = [];
      for (const x of bt) bm[x.path] = x.a;
      for (const x of cur) {
        cm[x.path] = 1; const b = bm[x.path];
        if (!b) { L.push('+ ' + x.path + ': ' + x.a.t + ' ' + x.a.s + ' ' + fmt(x.a)); continue; }
        const ch = []; for (const k of new Set(Object.keys(b).concat(Object.keys(x.a)))) { if (k === 'n' && !x.path) continue; if (String(b[k]) !== String(x.a[k])) ch.push(k + '=' + (x.a[k] === undefined ? '-' : x.a[k])); }
        if (ch.length) L.push('~ ' + (x.path || '.') + ': ' + ch.join('; '));
      }
      for (const x of bt) if (!cm[x.path]) L.push('- ' + x.path);
      B.push('# ' + v.name + (bv.id !== dv.id ? ' (vs ' + bv.name + ')' : '') + (L.length ? '\n' + L.slice(0, MAXN).join('\n') + (L.length > MAXN ? '\n… +' + (L.length - MAXN) : '') : ' : same'));
    }
  }
  return B;
}
const bytes = s => s.length + (s.match(/[^\x00-\x7f]/g) || []).length * 2 + (s.match(/["\\\n]/g) || []).length;
let out = '', size = 0, next = null;
loop: for (let ci = CI0; ci < IDS.length; ci++) {
  const B = await blocks(IDS[ci]);
  for (let bi = (ci === CI0 ? BI0 : 0); bi < B.length; bi++) {
    const b = bytes(B[bi]) + 2;
    if (size + b > LIMIT && size > 0) { next = [ci, bi]; break loop; }
    out += B[bi] + '\n'; size += b;
  }
}
return { next, out };
