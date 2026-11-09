(function(root){
'use strict';
const STORAGE_KEY='wfs.deviceProfiles.v1';
const SHAPES=['circle','square','rectangle','rounded-square','rounded-rectangle'];
const COLOR_FORMATS=['RGB565','RGB888','INDEXED8','MONO1'];
const DEFAULT_PROFILE={
 profileVersion:1,id:'esp32-c3-gc9a01-240',name:'ESP32-C3 · GC9A01',
 manufacturer:'Espressif',deviceModel:'ESP32-C3 DevKitM-1',
 display:{width:240,height:240,shape:'circle',driver:'GC9A01',colorFormat:'RGB565',rotation:0,centerX:120,centerY:120,safeArea:{top:0,right:0,bottom:0,left:0}}
};
function clone(value){return JSON.parse(JSON.stringify(value))}
function normalize(profile){
 const p=clone(profile||DEFAULT_PROFILE),d=p.display||(p.display={});
 p.profileVersion=1;
 p.id=String(p.id||'custom-profile').slice(0,64);
 p.name=String(p.name||'Custom display').trim().slice(0,80)||'Custom display';
 d.width=Number(d.width);d.height=Number(d.height);
 d.shape=SHAPES.includes(d.shape)?d.shape:'rectangle';
 d.colorFormat=COLOR_FORMATS.includes(d.colorFormat)?d.colorFormat:'RGB565';
 d.rotation=[0,90,180,270].includes(Number(d.rotation))?Number(d.rotation):0;
 d.centerX=Number.isFinite(Number(d.centerX))?Number(d.centerX):d.width/2;
 d.centerY=Number.isFinite(Number(d.centerY))?Number(d.centerY):d.height/2;
 const safe=d.safeArea||{};
 d.safeArea={top:Number(safe.top)||0,right:Number(safe.right)||0,bottom:Number(safe.bottom)||0,left:Number(safe.left)||0};
 return p;
}
function validate(profile){
 const raw=profile&&typeof profile==='object'?profile:{},rawDisplay=raw.display&&typeof raw.display==='object'?raw.display:{};
 const p=normalize(raw),d=p.display,errors=[];
 if(raw.profileVersion!==1)errors.push('Unsupported Device Profile version.');
 if(typeof raw.id!=='string'||!raw.id.trim()||raw.id.length>64)errors.push('Profile ID must contain 1 to 64 characters.');
 if(typeof raw.name!=='string'||!raw.name.trim()||raw.name.length>80)errors.push('Profile name must contain 1 to 80 characters.');
 if(!Number.isInteger(Number(rawDisplay.width))||Number(rawDisplay.width)<1||Number(rawDisplay.width)>1024)errors.push('Width must be an integer from 1 to 1024 pixels.');
 if(!Number.isInteger(Number(rawDisplay.height))||Number(rawDisplay.height)<1||Number(rawDisplay.height)>1024)errors.push('Height must be an integer from 1 to 1024 pixels.');
 if(!SHAPES.includes(rawDisplay.shape))errors.push('Choose a supported display shape.');
 if(d.shape==='circle'&&d.width!==d.height)errors.push('A circular display must have equal width and height.');
 if(!COLOR_FORMATS.includes(rawDisplay.colorFormat))errors.push('Choose a supported color format.');
 if(![0,90,180,270].includes(Number(rawDisplay.rotation)))errors.push('Rotation must be 0, 90, 180 or 270 degrees.');
 const rawSafe=rawDisplay.safeArea||{},s=d.safeArea;
 for(const key of ['top','right','bottom','left']){const value=rawSafe[key]===undefined?0:Number(rawSafe[key]);if(!Number.isFinite(value)||value<0)errors.push('Safe-area margins must be non-negative numbers.');}
 if(s.left+s.right>=d.width||s.top+s.bottom>=d.height)errors.push('Safe-area margins leave no usable display area.');
 if(!Number.isFinite(d.centerX)||d.centerX<0||d.centerX>d.width)errors.push('Center X must be within the display.');
 if(!Number.isFinite(d.centerY)||d.centerY<0||d.centerY>d.height)errors.push('Center Y must be within the display.');
 return errors;
}
function migrateObjects(objects,from,to,strategy){
 const oldW=Math.max(1,Number(from.width)||1),oldH=Math.max(1,Number(from.height)||1);
 const sx=Number(to.width)/oldW,sy=Number(to.height)/oldH;
 return (objects||[]).map(object=>{
  const copy=clone(object),t=copy.transform||{};
  if(strategy==='scale'){
   if(Number.isFinite(t.x))t.x=t.x*sx;
   if(Number.isFinite(t.width))t.width=t.width*sx;
   if(Number.isFinite(t.y))t.y=t.y*sy;
   if(Number.isFinite(t.height))t.height=t.height*sy;
   if(copy.textStyle){copy.textStyle.fontSize=(Number(copy.textStyle.fontSize)||16)*sy;copy.textStyle.scaleX=(Number(copy.textStyle.scaleX)||1)*(sx/sy);if(Number.isFinite(copy.textStyle.letterSpacing))copy.textStyle.letterSpacing*=sx;if(Number.isFinite(copy.textStyle.scaleY))copy.textStyle.scaleY=copy.textStyle.scaleY}
  }
  copy.transform=t;return copy;
 });
}
function compatibleWithCurrentFirmware(profile){
 const p=normalize(profile),d=p.display;
 return p.id==='esp32-c3-gc9a01-240'&&d.width===240&&d.height===240&&d.shape==='circle'&&d.rotation===0&&d.colorFormat==='RGB565';
}
const api={STORAGE_KEY,SHAPES,COLOR_FORMATS,DEFAULT_PROFILE,clone,normalize,validate,migrateObjects,compatibleWithCurrentFirmware};
root.WFSDeviceProfile=api;
if(typeof module!=='undefined'&&module.exports)module.exports=api;
})(typeof globalThis!=='undefined'?globalThis:this);
