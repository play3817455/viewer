#!/usr/bin/env node
// Convert one parsed client DXG + VRA pair into glTF 2.0 for Unreal Interchange.
// Usage: node export_gltf.js <unpackdat> <model.dxg> <material.vra> <output-dir>
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const { spawnSync } = require('child_process');
// DXG positions are treated as centimeters in the Unreal pilot: glTF stores meters,
// then the UE importer converts the model back to centimeters.
const DXG_TO_GLTF_METERS = 0.01;

function fail(s) { console.error(s); process.exit(2); }
const [rootArg, dxgArg, vraArg, outArg] = process.argv.slice(2);
if (!rootArg || !dxgArg || !vraArg || !outArg) fail('usage: node export_gltf.js <unpackdat> <model.dxg> <material.vra> <output-dir>');
const root = path.resolve(rootArg), dxgPath = path.resolve(dxgArg), vraPath = path.resolve(vraArg), outDir = path.resolve(outArg);
const parserPath = path.resolve(__dirname, '../../../iconmatch/ms_model.js');
if (!fs.existsSync(parserPath)) fail(`existing DXG parser not found: ${parserPath}`);
if (!dxgPath.startsWith(root + path.sep) || !vraPath.startsWith(root + path.sep)) fail('DXG/VRA must be inside unpackdat');
fs.mkdirSync(outDir, { recursive: true });

const sandbox = { window: {}, console, TextEncoder, TextDecoder, Uint8Array, Int32Array, Float32Array, Uint16Array,
  ArrayBuffer, DataView, Map, Set, Math, Number, String, Boolean, Error, Promise,
  fetch: () => Promise.reject(new Error('network disabled')) };
vm.runInNewContext(fs.readFileSync(parserPath, 'utf8'), sandbox, { filename: parserPath });
const api = sandbox.window.MSModel;
const dxgBytes = fs.readFileSync(dxgPath);
const dxgBuffer = dxgBytes.buffer.slice(dxgBytes.byteOffset, dxgBytes.byteOffset + dxgBytes.byteLength);
const model = api.parseDXG(dxgBuffer);
if (!model || !model.meshes?.length) fail('DXG parser returned no meshes');
const vra = api.parseVRA(fs.readFileSync(vraPath, 'utf8'));

const binParts = [];
let binLen = 0;
const views = [], accessors = [];
function addAccessor(typed, componentType, type, target, min, max) {
  while (binLen % 4) { binParts.push(Buffer.from([0])); binLen++; }
  const data = Buffer.from(typed.buffer, typed.byteOffset, typed.byteLength);
  const offset = binLen; binParts.push(data); binLen += data.length;
  const view = { buffer: 0, byteOffset: offset, byteLength: data.length };
  if (target) view.target = target;
  const viewIndex = views.push(view) - 1;
  const acc = { bufferView: viewIndex, componentType, count: typed.length / ({SCALAR:1,VEC2:2,VEC3:3,VEC4:4,MAT4:16}[type]), type };
  if (min) acc.min = min;
  if (max) acc.max = max;
  return accessors.push(acc) - 1;
}
function bounds(a, stride) {
  const lo = Array(stride).fill(Infinity), hi = Array(stride).fill(-Infinity);
  for (let i=0; i<a.length; i+=stride) for (let j=0;j<stride;j++) { lo[j]=Math.min(lo[j],a[i+j]); hi[j]=Math.max(hi[j],a[i+j]); }
  return [lo,hi];
}
function mult4(a,b) {
  const o = new Float32Array(16);
  for(let c=0;c<4;c++) for(let r=0;r<4;r++) o[c*4+r]=a[r]*b[c*4]+a[4+r]*b[c*4+1]+a[8+r]*b[c*4+2]+a[12+r]*b[c*4+3];
  return o;
}
function invRigid(m) {
  const o = new Float32Array(16);
  o[0]=m[0];o[1]=m[4];o[2]=m[8]; o[4]=m[1];o[5]=m[5];o[6]=m[9]; o[8]=m[2];o[9]=m[6];o[10]=m[10];o[15]=1;
  o[12]=-(o[0]*m[12]+o[4]*m[13]+o[8]*m[14]); o[13]=-(o[1]*m[12]+o[5]*m[13]+o[9]*m[14]); o[14]=-(o[2]*m[12]+o[6]*m[13]+o[10]*m[14]);
  return o;
}
function quatFromMat(m) {
  const m00=m[0],m11=m[5],m22=m[10]; let x,y,z,w; const tr=m00+m11+m22;
  if(tr>0){let s=Math.sqrt(tr+1)*2;w=.25*s;x=(m[6]-m[9])/s;y=(m[8]-m[2])/s;z=(m[1]-m[4])/s;}
  else if(m00>m11&&m00>m22){let s=Math.sqrt(1+m00-m11-m22)*2;w=(m[6]-m[9])/s;x=.25*s;y=(m[4]+m[1])/s;z=(m[8]+m[2])/s;}
  else if(m11>m22){let s=Math.sqrt(1+m11-m00-m22)*2;w=(m[8]-m[2])/s;x=(m[4]+m[1])/s;y=.25*s;z=(m[9]+m[6])/s;}
  else {let s=Math.sqrt(1+m22-m00-m11)*2;w=(m[1]-m[4])/s;x=(m[8]+m[2])/s;y=(m[9]+m[6])/s;z=.25*s;}
  return [x,y,z,w];
}

const images = [], textures = [], materials = [];
const matIndexById = new Map();
const textureIndexByPath = new Map();
for (const [id, mat] of vra.mats) {
  const glMat = { name: `VRA_${id}`, pbrMetallicRoughness: { baseColorFactor: [0.8,0.8,0.8,1], metallicFactor: 0, roughnessFactor: 0.8 } };
  if (mat.texture) {
    let rel = mat.texture.replace(/\\/g,'/'); const mark = rel.toLowerCase().indexOf('unpackdat/'); if(mark>=0) rel=rel.slice(mark+10); rel=rel.replace(/^\/+/, '');
    const texPath = path.resolve(root, ...rel.split('/'));
    if (texPath.startsWith(root + path.sep) && fs.existsSync(texPath)) {
      let texIndex=textureIndexByPath.get(texPath.toLowerCase());
      if(texIndex===undefined){
        const pngName = `${path.basename(texPath, path.extname(texPath))}.png`, pngPath=path.join(outDir,pngName);
        const cvt=spawnSync('python',['-c','from PIL import Image; import sys; im=Image.open(sys.argv[1]); im.load(); im.convert("RGBA").save(sys.argv[2])',texPath,pngPath],{encoding:'utf8'});
        if(cvt.status===0){const imageIndex=images.push({uri:pngName,name:path.basename(pngName)})-1; texIndex=textures.push({source:imageIndex})-1; textureIndexByPath.set(texPath.toLowerCase(),texIndex);}
        else console.warn(`texture conversion skipped: ${rel}: ${cvt.stderr.trim()}`);
      }
      if(texIndex!==undefined) glMat.pbrMetallicRoughness.baseColorTexture={index:texIndex};
    } else console.warn(`texture missing: ${rel}`);
  }
  if(mat.twoSided) glMat.doubleSided=true;
  if(mat.alphaTest) { glMat.alphaMode='MASK'; glMat.alphaCutoff=0.5; }
  else if(mat.blend && mat.blend!=='none') glMat.alphaMode='BLEND';
  matIndexById.set(id,materials.push(glMat)-1);
}

const nodes = [], glMeshes = [], primitives = [];
let skin = null;
const animations = [];
if (model.bones?.length) {
  const boneNodeIndices = model.bones.map(b=>nodes.push({name:b.name})-1);
  const inverseBind = model.bones.map(b=>{const m=new Float32Array(b.m);m[12]*=DXG_TO_GLTF_METERS;m[13]*=DXG_TO_GLTF_METERS;m[14]*=DXG_TO_GLTF_METERS;return m;});
  const globalBind = inverseBind.map(invRigid);
  for(let i=0;i<model.bones.length;i++){
    const parent=model.bones[i].parent;
    const local=parent>=0&&parent<model.bones.length?mult4(inverseBind[parent],globalBind[i]):globalBind[i];
    nodes[boneNodeIndices[i]].translation=[local[12],local[13],local[14]];
    nodes[boneNodeIndices[i]].rotation=quatFromMat(local);
    if(parent>=0&&parent<model.bones.length) (nodes[boneNodeIndices[parent]].children ||= []).push(boneNodeIndices[i]);
  }
  const ibmData=new Float32Array(model.bones.length*16); inverseBind.forEach((m,i)=>ibmData.set(m,i*16));
  const ibmAccessor=addAccessor(ibmData,5126,'MAT4',null);
  const rootBone=boneNodeIndices.find((n,i)=>model.bones[i].parent<0);
  skin={name:'DXG_Skeleton',joints:boneNodeIndices,inverseBindMatrices:ibmAccessor,skeleton:rootBone};
}
for(const mesh of model.meshes){
  const attrs={};
  const positions=new Float32Array(mesh.pos.length); for(let i=0;i<mesh.pos.length;i++) positions[i]=mesh.pos[i]*DXG_TO_GLTF_METERS;
  const [lo,hi]=bounds(positions,3); attrs.POSITION=addAccessor(positions,5126,'VEC3',34962,lo,hi);
  attrs.NORMAL=addAccessor(mesh.nrm,5126,'VEC3',34962);
  attrs.TEXCOORD_0=addAccessor(mesh.uv,5126,'VEC2',34962);
  if(skin&&mesh.vbone){
    const joints=new Uint16Array(mesh.nVerts*4), weights=new Float32Array(mesh.nVerts*4);
    for(let i=0;i<mesh.nVerts;i++){const j=mesh.vbone[i]>=0?mesh.vbone[i]:0;joints[i*4]=j;weights[i*4]=1;}
    attrs.JOINTS_0=addAccessor(joints,5123,'VEC4',34962); attrs.WEIGHTS_0=addAccessor(weights,5126,'VEC4',34962);
  }
  const indices=mesh.idx instanceof Uint16Array?mesh.idx:new Uint16Array(mesh.idx);
  const indexAcc=addAccessor(indices,5123,'SCALAR',34963);
  const matId=vra.meshMat.has(mesh.name)?vra.meshMat.get(mesh.name):null;
  const primitive={attributes:attrs,indices:indexAcc,mode:4,extras:{clientMeshName:mesh.name}};
  if(matId!==null&&matIndexById.has(matId)) primitive.material=matIndexById.get(matId);
  primitives.push(primitive);
}
const boneRoots=model.bones.map((b,i)=>b.parent<0?i:-1).filter(i=>i>=0).map(i=>i);
const meshIndex=glMeshes.push({name:'00Raiser_1132252',primitives})-1;
const meshNode=nodes.push({name:'00Raiser_1132252',mesh:meshIndex,...(skin?{skin:0}:{})})-1;
const animationDir=path.join(path.dirname(path.dirname(dxgPath)),'anim');
if(skin&&fs.existsSync(animationDir)){
  const boneByName=new Map(model.bones.map((b,i)=>[b.name,i]));
  for(const file of fs.readdirSync(animationDir).filter(f=>f.toLowerCase().endsWith('.mrb')).sort()){
    const bytes=fs.readFileSync(path.join(animationDir,file)); const ab=bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);
    const motion=api.parseMRB(ab); if(!motion||!motion.nKeys||!motion.nBones) continue;
    const seconds=new Float32Array(motion.times.length); for(let i=0;i<seconds.length;i++) seconds[i]=motion.times[i]/1000;
    const timeAccessor=addAccessor(seconds,5126,'SCALAR',null,[Math.min(...seconds)],[Math.max(...seconds)]);
    const channels=[],samplers=[];
    for(let b=0;b<motion.names.length;b++){
      const node=boneByName.get(motion.names[b]); if(node===undefined) continue;
      const rotations=new Float32Array(motion.nKeys*4),base=b*motion.nKeys;
      for(let k=0;k<motion.nKeys;k++){const source=motion.idxRot[base+k]*4; rotations.set(motion.rotKeys.subarray(source,source+4),k*4);}
      const output=addAccessor(rotations,5126,'VEC4',null);
      const sampler=samplers.push({input:timeAccessor,output,interpolation:'LINEAR'})-1;
      channels.push({sampler,target:{node,path:'rotation'}});
    }
    if(channels.length) animations.push({name:path.basename(file,'.mrb'),samplers,channels});
  }
}
const gltf={asset:{version:'2.0',generator:'machine_console DXG/VRA/MRB converter'},scene:0,scenes:[{nodes:[...boneRoots,meshNode]}],nodes,meshes:glMeshes,materials,textures,images,
  accessors,bufferViews:views,buffers:[{uri:'machine.bin',byteLength:binLen}],samplers:[{magFilter:9729,minFilter:9987,wrapS:10497,wrapT:10497}]};
if(textures.length) textures.forEach(t=>t.sampler=0);
if(skin) gltf.skins=[skin];
if(animations.length) gltf.animations=animations;
fs.writeFileSync(path.join(outDir,'machine.bin'),Buffer.concat(binParts,binLen));
fs.writeFileSync(path.join(outDir,'00Raiser_1132252.gltf'),JSON.stringify(gltf,null,2));
fs.writeFileSync(path.join(outDir,'export_report.json'),JSON.stringify({sourceModel:dxgPath,sourceMaterial:vraPath,meshes:model.meshes.length,bones:model.bones.length,materials:materials.length,textures:images.length,animations:animations.length,skinnedMeshes:model.meshes.filter(m=>m.vbone).length,unitScale:DXG_TO_GLTF_METERS,notes:['DXG/VRA/MRB parsers reused from iconmatch without modifying it','DXG spatial units were scaled by 0.01 to glTF meters for UE centimeter import; verify visually in UE','MRB rotation keys exported as individually named clips; action IDs remain manually selectable and are not guessed','Additive materials are represented as alpha blend; exact additive appearance needs UE material adjustment']},null,2));
console.log(`wrote 00Raiser_1132252.gltf: ${model.meshes.length} meshes, ${model.bones.length} bones, ${materials.length} materials, ${images.length} textures, ${animations.length} animation clips`);
