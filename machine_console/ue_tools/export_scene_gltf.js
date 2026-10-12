#!/usr/bin/env node
// Bake one client scene VRA set into a single static glTF mesh for UE Interchange.
// Usage: node export_scene_gltf.js <unpackdat> <world/field/F01> <output-dir> [F01_00_grd.vra F01_00_obj.vra]
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');
const { spawnSync } = require('child_process');
const SCALE = 0.01; // DXG scene units -> glTF meters -> UE centimeters.
function fail(s) { console.error(s); process.exit(2); }
const args = process.argv.slice(2);
const collisionOnly = args.includes('--collision');
let mapId = null;
const positional = [];
for (let i = 0; i < args.length; i++) {
  if (args[i] === '--collision') continue;
  if (args[i] === '--map-id') { mapId = args[++i]; continue; }
  positional.push(args[i]);
}
const [rootArg, sceneArg, outArg, ...explicitFiles] = positional;
if (!rootArg || !sceneArg || !outArg) fail('usage: node export_scene_gltf.js <unpackdat> <scene-dir> <output-dir> [placement.vra ...]');
const root = path.resolve(rootArg), sceneDir = path.resolve(sceneArg), outDir = path.resolve(outArg);
if (!sceneDir.startsWith(root + path.sep)) fail('scene directory must be inside unpackdat');
const parserPath = path.resolve(__dirname, '../../../iconmatch/ms_model.js');
if (!fs.existsSync(parserPath)) fail(`DXG parser not found: ${parserPath}`);
fs.mkdirSync(outDir, { recursive: true });
const sandbox = { window: {}, console, TextEncoder, TextDecoder, Uint8Array, Int32Array, Float32Array, Uint16Array,
  ArrayBuffer, DataView, Map, Set, Math, Number, String, Boolean, Error, Promise,
  fetch: () => Promise.reject(new Error('network disabled')) };
vm.runInNewContext(fs.readFileSync(parserPath, 'utf8'), sandbox, { filename: parserPath });
const api = sandbox.window.MSModel;

function csvFields(line) {
  const fields = []; let value = '', quoted = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (c === '"' && quoted && line[i + 1] === '"') { value += '"'; i++; }
    else if (c === '"') quoted = !quoted;
    else if (c === ',' && !quoted) { fields.push(value); value = ''; }
    else value += c;
  }
  fields.push(value);
  return fields;
}

let mapDefinition = null, missingMapResources = [];
let filesArg = explicitFiles;
if (mapId) {
  const mapCsv = path.join(root, 'settings', 'map.csv');
  if (!fs.existsSync(mapCsv)) fail(`map.csv not found: ${mapCsv}`);
  const mapRows = fs.readFileSync(mapCsv, 'utf8').replace(/^\uFEFF/, '').split(/\r?\n/).map(csvFields);
  const row = mapRows.find(fields => fields[0] === String(mapId));
  if (!row) fail(`map id ${mapId} not found in ${mapCsv}`);
  const mapSettings = row.slice(11).map((value, i) => ({
    column: i + 11, headers: mapRows.slice(0, 4).map(header => header[i + 11] || ''), value,
  }));
  const fieldRoot = path.resolve(root, 'world/field');
  const resolveFieldResource = value => path.resolve(fieldRoot, value.replace(/\\/g, '/'));
  const ground = resolveFieldResource(row[1]);
  const objects = resolveFieldResource(row[2]);
  const secondBox = resolveFieldResource(row[3]);
  const areaPattern = row[9] || '';
  const areaCount = Number(row[10] || 0);
  const areaFiles = [];
  for (let i = 0; i < areaCount; i++) {
    const areaPath = resolveFieldResource(areaPattern.replace('%02d', String(i).padStart(2, '0')));
    if (fs.existsSync(areaPath)) areaFiles.push(areaPath);
    else missingMapResources.push(path.relative(root, areaPath).replace(/\\/g, '/'));
  }
  for (const required of [ground, objects, secondBox]) {
    if (!required.startsWith(fieldRoot + path.sep)) fail(`map.csv field resource escapes world/field: ${required}`);
    if (!fs.existsSync(required)) fail(`map ${mapId} required placement resource missing: ${required}`);
  }
  const allFiles = [ground, objects, secondBox, ...areaFiles];
  filesArg = allFiles.map(file => path.relative(sceneDir, file));
  for (const file of filesArg) if (file.startsWith('..')) fail(`map ${mapId} placement is outside selected field folder: ${file}`);
  mapDefinition = {
    id: String(mapId), mapCsv: path.relative(root, mapCsv).replace(/\\/g, '/'),
    ground: row[1], objects: row[2], secondBox: row[3], sky: row[7] || '', hit: row[8] || '',
    areaPattern, areaCount, areaFiles: areaFiles.map(file => path.relative(root, file).replace(/\\/g, '/')),
    mapSettings,
  };
  const skyAttr = path.resolve(root, 'world/sky', (row[7] || '').replace(/\\/g, '/'));
  const hitPath = path.resolve(fieldRoot, (row[8] || '').replace(/\\/g, '/'));
  mapDefinition.skyAttribute = fs.existsSync(skyAttr) ? path.relative(root, skyAttr).replace(/\\/g, '/') : null;
  mapDefinition.hitResource = fs.existsSync(hitPath) ? {
    path: path.relative(root, hitPath).replace(/\\/g, '/'), bytes: fs.statSync(hitPath).size,
    sha256: crypto.createHash('sha256').update(fs.readFileSync(hitPath)).digest('hex'),
  } : null;
  if (row[7] && !fs.existsSync(skyAttr)) missingMapResources.push(path.relative(root, skyAttr).replace(/\\/g, '/'));
  if (row[8] && !fs.existsSync(hitPath)) missingMapResources.push(path.relative(root, hitPath).replace(/\\/g, '/'));
}

function value(block, key, count) {
  const m = block.match(new RegExp('\\(' + key + '\\s+[^\\n]*?\\[1\\]\\s+([^)]*)\\)', 'i'));
  if (!m) return null;
  const a = m[1].split(/[\s,]+/).filter(Boolean).map(Number);
  return a.length >= count && a.slice(0, count).every(Number.isFinite) ? a.slice(0, count) : null;
}
function quoted(block, key) {
  const m = block.match(new RegExp('\\(' + key + '\\s+string\\[1\\]\\s+"([^"]*)"', 'i'));
  return m ? m[1] : null;
}
function parsePlacements(text) {
  const out = [];
  for (const b of text.match(/\{object[\s\S]*?\n\s*\}/g) || []) {
    const name = quoted(b, 'name'), category = quoted(b, 'category');
    if (!name || !category) continue;
    out.push({ name, category, pos: value(b, 'pos', 3) || [0, 0, 0], rot: value(b, 'rot', 4) || [0, 0, 0, 1], scale: value(b, 'scale', 3) || [1, 1, 1], physics: value(b, 'physics', 1)?.[0] ?? null });
  }
  return out;
}
function assetPaths(o) {
  if (o.attrPath) {
    const attr = path.resolve(o.attrPath);
    if (!attr.startsWith(root + path.sep) || !fs.existsSync(attr)) return { attr, dxg: null };
    const txt = fs.readFileSync(attr, 'utf8');
    const geom = quoted(txt, 'Geometry');
    const dxg = geom ? path.resolve(root, geom.replace(/\\/g, '/').replace(/^\/+/, '')) : null;
    return { attr, dxg: dxg && dxg.startsWith(root + path.sep) ? dxg : null };
  }
  const field = o.category.toLowerCase() === path.basename(sceneDir).toLowerCase();
  const attr = path.resolve(root, field ? `world/field/${o.category}/attr/${o.name}.vra` : `world/object/${o.category}/attr/${o.name}.vra`);
  if (!attr.startsWith(root + path.sep) || !fs.existsSync(attr)) return { attr, dxg: null };
  const txt = fs.readFileSync(attr, 'utf8');
  let geom = quoted(txt, 'Geometry');
  if (!geom && field && o.name.startsWith('FZ_')) geom = `world/field/${o.category}/model/${o.name.replace(/^FZ_/, 'FM_')}.dxg`;
  const dxg = geom ? path.resolve(root, geom.replace(/\\/g, '/').replace(/^\/+/, '')) : null;
  return { attr, dxg: dxg && dxg.startsWith(root + path.sep) ? dxg : null };
}
function quatRotate(v, q) {
  const [x, y, z, w] = q;
  const tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
  return [v[0] + w * tx + (y * tz - z * ty), v[1] + w * ty + (z * tx - x * tz), v[2] + w * tz + (x * ty - y * tx)];
}
function transformPoint(p, o) {
  const v = quatRotate([p[0] * o.scale[0], p[1] * o.scale[1], p[2] * o.scale[2]], o.rot);
  return [(v[0] + o.pos[0]) * SCALE, (v[1] + o.pos[1]) * SCALE, (v[2] + o.pos[2]) * SCALE];
}
function transformNormal(n, o) {
  const v = quatRotate([n[0] / (o.scale[0] || 1), n[1] / (o.scale[1] || 1), n[2] / (o.scale[2] || 1)], o.rot);
  const l = Math.hypot(...v) || 1;
  return v.map(x => x / l);
}

function parseDXGBytes(bytes) {
  const view = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
  try { return { model: api.parseDXG(view), normalizedBlocks: 0 }; }
  catch (firstError) {
    // World-field DXGs use type 0x201 for the same mesh-block body encoded as
    // type 1 elsewhere. Normalize only a private copy, never the source asset
    // or the shared iconmatch parser.
    if (bytes.length < 28 || bytes.toString('ascii', 0, 4) !== 'DXG ') throw firstError;
    const copy = Buffer.from(bytes);
    const nameBytes = copy.readUInt32LE(20);
    if (24 + nameBytes > copy.length) throw firstError;
    let offset = 24 + nameBytes, normalized = 0;
    while (offset + 8 <= copy.length) {
      const type = copy.readUInt32LE(offset), size = copy.readUInt32LE(offset + 4);
      if (size > copy.length - offset - 8) break;
      if (type === 0x201) { copy.writeUInt32LE(1, offset); normalized++; }
      offset += 8 + size;
      if (type === 0 && size === 0) break;
    }
    if (!normalized) throw firstError;
    try { return { model: api.parseDXG(copy.buffer.slice(copy.byteOffset, copy.byteOffset + copy.byteLength)), normalizedBlocks: normalized }; }
    catch (secondError) { throw new Error(`${firstError.message}; 0x201 mesh-block parse failed: ${secondError.message}`); }
  }
}

const files = filesArg.length ? filesArg : fs.readdirSync(sceneDir).filter(f => /^F01_00_(grd|obj)\.vra$/i.test(f)).sort();
const placements = [];
for (const file of files) {
  const p = path.resolve(sceneDir, file);
  if (!p.startsWith(sceneDir + path.sep) || !fs.existsSync(p)) fail(`placement file missing/outside scene directory: ${file}`);
  placements.push(...parsePlacements(fs.readFileSync(p, 'utf8')).map(o => ({ ...o, sourceFile: file })));
}
const sourcePlacementCount = placements.length;
const mapPlacementCount = placements.length;
if (mapDefinition?.skyAttribute && !collisionOnly) {
  const skyAttr = path.resolve(root, 'world/sky', mapDefinition.sky);
  placements.push({ name: path.basename(skyAttr, '.vra'), category: 'sky', pos: [0, 0, 0], rot: [0, 0, 0, 1],
    scale: [1, 1, 1], physics: 0, attrPath: skyAttr, mapSky: true });
}
if (collisionOnly) {
  // Terrain and world objects with VRA physics=1 are the collision authority for this source block.
  for (let i = placements.length - 1; i >= 0; i--) if (placements[i].physics !== 1) placements.splice(i, 1);
}
if (!placements.length) fail('no {object} placements found');

const materialRows = new Map(), buckets = new Map(), textureFiles = new Map();
const failures = [], usedModels = new Set();
const placementAudit = [];
let totalMeshes = 0, totalVertices = 0, totalTriangles = 0, loadedInstances = 0, normalizedDxgBlocks = 0;
function getMaterial(attrPath, matId, mat) {
  if (collisionOnly) {
    if (!buckets.has(0)) buckets.set(0, { positions: [], normals: [], uvs: [], indices: [] });
    return 0;
  }
  const key = `${attrPath.toLowerCase()}#${matId}`;
  if (materialRows.has(key)) return materialRows.get(key).index;
  const row = { name: `VRA_${path.basename(attrPath, '.vra')}_${matId}`, doubleSided: !!mat?.twoSided,
    alphaMode: mat?.alphaTest ? 'MASK' : (mat?.blend && mat.blend !== 'none' ? 'BLEND' : 'OPAQUE'), baseColorTexture: null,
    pbrMetallicRoughness: { baseColorFactor: [0.8, 0.8, 0.8, 1], metallicFactor: 0, roughnessFactor: 0.8 } };
  const tex = mat?.texture;
  if (tex) {
    let rel = tex.replace(/\\/g, '/'); const marker = rel.toLowerCase().indexOf('unpackdat/'); if (marker >= 0) rel = rel.slice(marker + 10);
    const texPath = path.resolve(root, ...rel.replace(/^\/+/, '').split('/'));
    if (texPath.startsWith(root + path.sep) && fs.existsSync(texPath)) {
      let uri = textureFiles.get(texPath.toLowerCase());
      if (!uri) {
        uri = `${crypto.createHash('sha1').update(texPath.toLowerCase()).digest('hex').slice(0, 12)}.png`;
        const dest = path.join(outDir, uri);
        const cvt = spawnSync('python', ['-c', 'from PIL import Image; import sys; im=Image.open(sys.argv[1]); im.load(); im.convert("RGBA").save(sys.argv[2])', texPath, dest], { encoding: 'utf8' });
        if (cvt.status === 0) textureFiles.set(texPath.toLowerCase(), uri);
        else { failures.push({ texture: rel, reason: cvt.stderr.trim() }); uri = null; }
      }
      if (uri) row.baseColorTexture = uri;
    } else failures.push({ texture: rel, reason: 'file missing or outside unpackdat' });
  }
  const index = materialRows.size;
  materialRows.set(key, { index, row });
  buckets.set(index, { positions: [], normals: [], uvs: [], indices: [] });
  return index;
}

for (const o of placements) {
  const { attr, dxg } = assetPaths(o);
  const audit = { sourceFile: o.sourceFile || null, object: `${o.category}/${o.name}`, name: o.name,
    position: o.pos, rotation: o.rot, scale: o.scale, physics: o.physics, sky: !!o.mapSky,
    attribute: path.relative(root, attr).replace(/\\/g, '/'), model: dxg ? path.relative(root, dxg).replace(/\\/g, '/') : null };
  if (!dxg || !fs.existsSync(dxg)) {
    const reason = 'attribute/model path missing'; failures.push({ ...audit, reason, attr, dxg });
    placementAudit.push({ ...audit, status: 'missing', reason }); continue;
  }
  let model, vra;
  try {
    const bytes = fs.readFileSync(dxg); const parsed = parseDXGBytes(bytes); model = parsed.model;
    audit.normalizedDxgBlocks = parsed.normalizedBlocks; normalizedDxgBlocks += parsed.normalizedBlocks;
    vra = api.parseVRA(fs.readFileSync(attr, 'utf8'));
  } catch (e) {
    const specialFormat = fs.statSync(dxg).size === 96 && fs.readFileSync(dxg).toString('ascii', 0, 4) === 'DXG ';
    const reason = specialFormat ? `non-mesh special DXG (96-byte variant): ${String(e.message || e)}` : String(e.message || e);
    failures.push({ ...audit, reason }); placementAudit.push({ ...audit, status: 'skipped', reason }); continue;
  }
  if (!model?.meshes?.length) {
    const reason = 'DXG has no parseable mesh'; failures.push({ ...audit, reason });
    placementAudit.push({ ...audit, status: 'skipped', reason }); continue;
  }
  placementAudit.push({ ...audit, status: 'loaded', meshCount: model.meshes.length });
  loadedInstances++; usedModels.add(dxg); totalMeshes += model.meshes.length;
  const mirrored = o.scale[0] * o.scale[1] * o.scale[2] < 0;
  for (const mesh of model.meshes) {
    const matId = vra.meshMat.has(mesh.name) ? vra.meshMat.get(mesh.name) : null;
    const mat = matId === null ? null : vra.mats.get(matId);
    const bucket = buckets.get(getMaterial(attr, matId ?? 'default', mat));
    const base = bucket.positions.length / 3;
    for (let i = 0; i < mesh.pos.length; i += 3) bucket.positions.push(...transformPoint([mesh.pos[i], mesh.pos[i + 1], mesh.pos[i + 2]], o));
    for (let i = 0; i < mesh.nrm.length; i += 3) bucket.normals.push(...transformNormal([mesh.nrm[i], mesh.nrm[i + 1], mesh.nrm[i + 2]], o));
    for (let i = 0; i < mesh.uv.length; i++) bucket.uvs.push(mesh.uv[i]);
    for (let i = 0; i < mesh.idx.length; i += 3) {
      bucket.indices.push(base + mesh.idx[i], base + mesh.idx[i + (mirrored ? 2 : 1)], base + mesh.idx[i + (mirrored ? 1 : 2)]);
    }
    totalVertices += mesh.nVerts; totalTriangles += mesh.idx.length / 3;
  }
}

const binParts = []; let binLen = 0; const views = [], accessors = [];
function addAccessor(data, componentType, type, target, min, max) {
  while (binLen % 4) { binParts.push(Buffer.from([0])); binLen++; }
  const b = Buffer.from(data.buffer, data.byteOffset, data.byteLength), offset = binLen; binParts.push(b); binLen += b.length;
  const view = { buffer: 0, byteOffset: offset, byteLength: b.length }; if (target) view.target = target;
  const vi = views.push(view) - 1, n = { SCALAR: 1, VEC2: 2, VEC3: 3 }[type];
  const a = { bufferView: vi, componentType, count: data.length / n, type }; if (min) a.min = min; if (max) a.max = max;
  return accessors.push(a) - 1;
}
const primitives = [];
for (const [materialIndex, bucket] of buckets) {
  if (!bucket.indices.length) continue;
  const positions = new Float32Array(bucket.positions), normals = new Float32Array(bucket.normals), uvs = new Float32Array(bucket.uvs), indices = new Uint32Array(bucket.indices);
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < positions.length; i += 3) for (let j = 0; j < 3; j++) { lo[j] = Math.min(lo[j], positions[i + j]); hi[j] = Math.max(hi[j], positions[i + j]); }
  primitives.push({ attributes: { POSITION: addAccessor(positions, 5126, 'VEC3', 34962, lo, hi), NORMAL: addAccessor(normals, 5126, 'VEC3', 34962), TEXCOORD_0: addAccessor(uvs, 5126, 'VEC2', 34962) }, indices: addAccessor(indices, 5125, 'SCALAR', 34963), material: materialIndex, mode: 4 });
}
if (!primitives.length) fail('no valid geometry survived parsing');
const assetName = mapId ? `Map_${mapId}_${collisionOnly ? 'collision' : 'scene'}` : (collisionOnly ? 'F01_00_collision' : 'F01_00_scene');
const binName = `${assetName}.bin`;
fs.writeFileSync(path.join(outDir, binName), Buffer.concat(binParts));
const images = [...textureFiles.values()].map(uri => ({ uri }));
const textures = images.map((_, i) => ({ source: i }));
const materials = [...materialRows.values()].map(x => {
  const m = x.row, uri = m.baseColorTexture; delete m.baseColorTexture;
  if (uri) m.pbrMetallicRoughness.baseColorTexture = { index: images.findIndex(i => i.uri === uri) };
  if (m.alphaMode === 'MASK') m.alphaCutoff = 0.5;
  return m;
});
if (collisionOnly && !materials.length) materials.push({ name: 'CollisionOnly', pbrMetallicRoughness: { baseColorFactor: [1, 1, 1, 1], metallicFactor: 0, roughnessFactor: 1 } });
const gltf = { asset: { version: '2.0', generator: 'machine_console DXG/VRA scene exporter' }, scene: 0,
  scenes: [{ nodes: [0] }], nodes: [{ name: assetName, mesh: 0 }], meshes: [{ name: assetName, primitives }],
  buffers: [{ uri: binName, byteLength: binLen }], bufferViews: views, accessors,
  materials, images, textures };
fs.writeFileSync(path.join(outDir, `${assetName}.gltf`), JSON.stringify(gltf));
const report = { scene: mapId ? `map_${mapId}` : 'F01_00', mapDefinition, missingMapResources,
  mode: collisionOnly ? 'physics=1 collision' : 'visual', placementFiles: files,
  sourcePlacementCount, mapPlacementCount, selectedPlacementCount: placements.length, loadedInstances,
  uniqueModels: usedModels.size, meshes: totalMeshes, normalizedDxgBlocks, sourceVertexCount: totalVertices, triangles: totalTriangles,
  materialCount: materials.length, textures: textureFiles.size, skipped: failures };
fs.writeFileSync(path.join(outDir, collisionOnly ? 'collision_export_report.json' : 'scene_export_report.json'), JSON.stringify(report, null, 2));
if (mapId && !collisionOnly) fs.writeFileSync(path.join(outDir, `Map_${mapId}_placement_audit.json`), JSON.stringify(placementAudit, null, 2));
console.log(JSON.stringify({ ...report, skipped: failures.length, binaryBytes: binLen }, null, 2));
