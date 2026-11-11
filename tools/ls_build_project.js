// Lens Studio editor script (run via `Lens Studio --exec run-script -f this -a "<stageDir> <outDir>"`).
// Creates the minidoom project from scratch: assets, cameras, regions, script object.
// Everything visual is built at runtime by Main.js/Ui.js; this only wires the static pieces.

const [STAGE, OUT] = args[0].split(" ");
const PRESETS = "/Applications/Lens Studio.app/Contents/JsPlugins/PresetsRegistry";
const FACE_LAYER = 5;
const GAME_LAYER = 6;

function log(s) { console.log("BUILD " + s); }
function srcPath(dir) { return new Editor.Model.SourcePath(new Editor.Path(dir), Editor.Model.SourceRootDirectory.Assets); }

async function importFile(am, abs, dir) {
  for (let attempt = 1; attempt <= 3; attempt++) {
    const r = await am.importExternalFileAsync(new Editor.Path(abs), srcPath(dir), Editor.Model.ResultType.Auto);
    if (r && r.primary) return r.primary;
    log("import retry " + attempt + ": " + abs);
    await new Promise(res => setTimeout(res, 500));
  }
  throw new Error("import failed: " + abs);
}

function orthoCamera(scene, obj, renderTarget, layerSet, order) {
  const cam = obj.addComponent("Camera");
  cam.cameraType = Editor.Components.CameraType.Orthographic;
  cam.renderTarget = renderTarget;
  cam.renderOrder = order;
  cam.renderLayer = layerSet;
  cam.size = 20.0;
  cam.near = -1.0;
  cam.far = 200.0;
  obj.layers = layerSet;
  obj.addComponent("Canvas");
  return cam;
}

function screenChild(scene, parent, name, layerSet) {
  const o = scene.addSceneObject(parent);
  o.name = name;
  o.layers = layerSet;
  const st = o.addComponent("ScreenTransform");
  return { obj: o, st: st };
}

try {
  const model = pluginSystem.findInterface(Editor.Model.IModel);
  model.setDefaultProject();
  let project = model.project;
  project.saveTo(new Editor.Path(OUT + "/minidoom.esproj"));
  project = model.project;
  const am = project.assetManager;
  const scene = project.scene;
  log("project at " + project.projectFile);

  // ---- assets
  const scripts = {};
  for (const f of ["doomEngine.js", "wadData.js", "LensState.js", "BuildConfig.js", "DebugCapture.js", "Ui.js", "Main.js"]) {
    scripts[f] = await importFile(am, STAGE + "/" + f, "Scripts");
    log("imported " + f);
  }
  const pass = await importFile(am, STAGE + "/flat.graphShader", "Materials");
  let devCam = am.assets.find(a => a.getTypeName() === "DeviceCameraTexture");
  if (!devCam) devCam = am.createNativeAsset("DeviceCameraTexture", "Device Camera Texture", srcPath("Textures"));
  const faceRT = am.createNativeAsset("RenderTarget", "FaceRT", srcPath("Textures"));
  faceRT.useScreenResolution = false;
  faceRT.resolution = new Editor.Size(56, 62);   // = FACE_W x FACE_H in engine/lens/lens_hud.h
  log("face textures");

  // Unlit image materials from Snap's flat graph. The graph samples baseTex only with
  // ENABLE_BASE_TEX defined; filtering lives on the TextureParameter's sampler.
  function imageMaterial(name, tex, filtering, wrap) {
    const m = am.createNativeAsset("Material", name, srcPath("Materials"));
    const p = m.addPass(pass);
    p.depthTest = false;
    p.depthWrite = false;
    p.blendMode = Editor.Assets.BlendMode.PremultipliedAlphaAuto;
    const defs = p.defines; defs.push("ENABLE_BASE_TEX"); p.defines = defs;
    const tp = new Editor.Assets.TextureParameter(tex.id);
    const smp = tp.sampler;
    smp.filteringMode = filtering;
    smp.mipmapsEnabled = false;
    smp.wrapModeU = wrap || Editor.Assets.WrapMode.ClampToEdge;
    smp.wrapModeV = wrap || Editor.Assets.WrapMode.ClampToEdge;
    tp.sampler = smp;
    p.baseTex = tp;
    return m;
  }
  imageMaterial("ScreenMat", devCam, Editor.Assets.FilteringMode.Nearest);   // game view (texture set at runtime)
  imageMaterial("UiMat", devCam, Editor.Assets.FilteringMode.Bilinear);      // buttons, backdrop
  // Face source: the whole device camera image. Main.js positions it under the Face Camera so a
  // window around the tracked face fills FaceRT, kept inside the camera image (never off-frame).
  const faceMat = imageMaterial("FaceMat", devCam, Editor.Assets.FilteringMode.Bilinear);
  log("materials");
  // Gameplay-only render target for the end-card replay (the 3D view, without face or controls).
  const gameRT = am.createNativeAsset("RenderTarget", "GameRT", srcPath("Textures"));
  gameRT.useScreenResolution = false;
  gameRT.resolution = new Editor.Size(640, 400);   // 2x the 320x200 view: crisp nearest-filtered pixels

  try {
    const sfx = am.createNativeAsset("AudioOutput", "SfxOutput", srcPath("Audio"));
    log("audio output asset: " + (sfx && sfx.getTypeName()));
  } catch (e) { log("AudioOutputAsset create failed: " + e); }
  try {
    const music = await importFile(am, STAGE + "/music_e1m1.mp3", "Audio");
    log("music " + music.getTypeName());
  } catch (e) { log("music import skipped: " + e); }

  // ---- scene
  for (const o of scene.rootSceneObjects) if (o.name === "Lighting") o.destroy();
  const orthoSet = Editor.Model.LayerSet.fromId(Editor.Model.LayerId.Ortho);
  scene.layers.add(new Editor.Model.LayerId(FACE_LAYER));
  const faceSet = Editor.Model.LayerSet.fromId(new Editor.Model.LayerId(FACE_LAYER));

  // Face camera: renders a window of the camera image into the 56x62 FaceRT (GPU downsample =
  // Doom-face pixels). Main.js moves "Face Source Image" to frame the face.
  const faceCamObj = scene.addSceneObject(null);
  faceCamObj.name = "Face Camera";
  orthoCamera(scene, faceCamObj, faceRT, faceSet, -10);
  const faceImg = screenChild(scene, faceCamObj, "Face Source Image", faceSet);
  const fimg = faceImg.obj.addComponent("Image");
  fimg.materials = [faceMat];
  fimg.stretchMode = Editor.Components.StretchMode.Stretch;
  log("face camera");

  // Game camera: Ui.js puts a copy of the 3D view under "Game Canvas" (layer GAME_LAYER), which
  // this camera renders into GameRT; Main.js records GameRT for the replay. Disabled at rest
  // (Main.js enables it only while recording).
  scene.layers.add(new Editor.Model.LayerId(GAME_LAYER));
  const gameSet = Editor.Model.LayerSet.fromId(new Editor.Model.LayerId(GAME_LAYER));
  const gameCamObj = scene.addSceneObject(null);
  gameCamObj.name = "Game Camera";
  orthoCamera(scene, gameCamObj, gameRT, gameSet, -9);
  screenChild(scene, gameCamObj, "Game Canvas", gameSet);
  log("game camera");

  // UI camera with backdrop + safe region.
  const uiCamObj = scene.addSceneObject(null);
  uiCamObj.name = "UI Camera";
  orthoCamera(scene, uiCamObj, scene.captureTarget, orthoSet, scene.mainCamera.renderOrder + 1);
  const backdrop = screenChild(scene, uiCamObj, "Backdrop", orthoSet);
  const bgReg = backdrop.obj.addComponent("ScreenRegionComponent");
  bgReg.region = Editor.Components.ScreenRegionType.FullFrame;
  const safe = screenChild(scene, uiCamObj, "SafeRegion", orthoSet);
  const safeReg = safe.obj.addComponent("ScreenRegionComponent");
  safeReg.region = Editor.Components.ScreenRegionType.SafeRender;
  const md = screenChild(scene, safe.obj, "MiniDoom", orthoSet);
  const sc = md.obj.addComponent("ScriptComponent");
  sc.scriptAsset = scripts["Main.js"];
  log("ui camera + script");

  // Face tracking: a Head binding under the main camera (the standard placement). Main.js uses its
  // onLandmarksUpdate to frame the face and to choose between the live face and the Doomguy fallback.
  const mainCamObj = scene.mainCamera.sceneObject;
  const headObj = scene.addSceneObject(mainCamObj);
  headObj.name = "Head Tracker";
  headObj.addComponent("Head");
  log("head tracker under " + mainCamObj.name);

  const mi = project.metaInfo;
  mi.lensName = "minidoom";
  // Open on the selfie (front) camera: the game's face slot needs the player's face.
  // The rear camera stays allowed; there the HUD face falls back to the Doomguy sprite.
  mi.activationCamera = Editor.Model.LensActivationCamera.Front;
  mi.lensApplicability = [Editor.Model.LensApplicability.Front, Editor.Model.LensApplicability.Back];
  mi.setIcon(new Editor.Path(STAGE + "/icon.png"));
  project.metaInfo = mi;
  log("meta: name " + project.metaInfo.lensName + ", icon set " + project.metaInfo.isIconSet +
      ", activation camera " + (project.metaInfo.activationCamera === Editor.Model.LensActivationCamera.Front ? "Front" : "Rear"));
  project.save();
  log("saved OK");
} catch (e) {
  log("FAILED " + e + "\n" + (e && e.stack));
}
