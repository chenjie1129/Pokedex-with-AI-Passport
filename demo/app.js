const STORAGE_KEY = "city-spirits-passport-mvp";
const CAPTURE_CYCLE_MS = 6000;
const TARGET_START = 25;
const TARGET_END = 75;
const THROW_DURATION_MS = 850;

const device = document.querySelector(".device");
const screen = document.querySelector("#screen");
const canvas = document.querySelector("#scene");
const ctx = canvas.getContext("2d");
const charmanderImage = new Image();
charmanderImage.src = "./assets/charmander.png?v=pokemon-004";
const eyebrow = document.querySelector("#eyebrow");
const title = document.querySelector("#title");
const message = document.querySelector("#message");
const meta = document.querySelector("#meta");
const screenAction = document.querySelector("#screenAction");
const saveState = document.querySelector("#saveState");
const captureMeter = document.querySelector("#captureMeter");
const captureMarker = document.querySelector("#captureMarker");
const captureCount = document.querySelector("#captureCount");
const okButton = document.querySelector("#okButton");
const upButton = document.querySelector("#upButton");
const downButton = document.querySelector("#downButton");
const resetButton = document.querySelector("#resetButton");
const debugPanel = document.querySelector("#debugPanel");
const debugMode = new URLSearchParams(window.location.search).get("debug") === "1";
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

let state = "home";
let homeSelection = 0;
let bestiarySelection = 0;
let pendingWrite = null;
let attempts = 3;
let markerValue = 0;
let captureStartedAt = 0;
let throwStartedAt = 0;
let throwWillSucceed = false;
let catchStartedAt = 0;
let animationFrame = 0;
let transitionTimers = [];

debugPanel.hidden = !debugMode;
document.querySelector(".workspace").classList.toggle("debug-mode", debugMode);

function loadSave() {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || "{}");
    return {
      captureCount: Number.isInteger(parsed.captureCount) && parsed.captureCount >= 0
        ? parsed.captureCount
        : 0,
      discoveryState: ["unknown", "seen", "captured"].includes(parsed.discoveryState)
        ? parsed.discoveryState
        : parsed.captured === true
          ? "captured"
          : "unknown",
      captured: parsed.captured === true,
      placeId: parsed.placeId === 1 ? 1 : null,
    };
  } catch {
    return {
      captureCount: 0,
      discoveryState: "unknown",
      captured: false,
      placeId: null,
    };
  }
}

let save = loadSave();

function persistSeen() {
  if (save.discoveryState !== "unknown") {
    return true;
  }
  const next = {
    ...save,
    discoveryState: "seen",
  };
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
    save = next;
    return true;
  } catch {
    return false;
  }
}

function persistCapture() {
  const next = {
    captureCount: save.captureCount + 1,
    discoveryState: "captured",
    captured: true,
    placeId: 1,
  };
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
    save = next;
    captureCount.textContent = String(save.captureCount);
    return true;
  } catch {
    return false;
  }
}

function clearTimers() {
  transitionTimers.forEach(window.clearTimeout);
  transitionTimers = [];
  window.cancelAnimationFrame(animationFrame);
}

function schedule(callback, delay) {
  transitionTimers.push(window.setTimeout(callback, delay));
}

function clearScene(color = "#c9eaf2") {
  ctx.fillStyle = color;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.imageSmoothingEnabled = true;
  ctx.imageSmoothingQuality = "high";
}

function pixel(x, y, width, height, color) {
  ctx.fillStyle = color;
  ctx.fillRect(x, y, width, height);
}

function roundedRect(x, y, width, height, radius, color) {
  ctx.beginPath();
  ctx.roundRect(x, y, width, height, radius);
  ctx.fillStyle = color;
  ctx.fill();
}

function drawGround() {
  const sky = ctx.createLinearGradient(0, 0, 0, 118);
  sky.addColorStop(0, "#bce6f2");
  sky.addColorStop(1, "#edf7e8");
  ctx.fillStyle = sky;
  ctx.fillRect(0, 0, 240, 118);

  ctx.fillStyle = "rgb(255 255 255 / 72%)";
  ctx.beginPath();
  ctx.ellipse(40, 31, 22, 8, 0, 0, Math.PI * 2);
  ctx.ellipse(58, 29, 16, 10, 0, 0, Math.PI * 2);
  ctx.ellipse(185, 42, 25, 8, 0, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "#86bd91";
  ctx.beginPath();
  ctx.moveTo(0, 103);
  ctx.quadraticCurveTo(38, 71, 77, 103);
  ctx.quadraticCurveTo(121, 65, 163, 103);
  ctx.quadraticCurveTo(205, 73, 240, 101);
  ctx.lineTo(240, 130);
  ctx.lineTo(0, 130);
  ctx.closePath();
  ctx.fill();

  const grass = ctx.createLinearGradient(0, 108, 0, 166);
  grass.addColorStop(0, "#79b979");
  grass.addColorStop(1, "#4c9667");
  ctx.fillStyle = grass;
  ctx.fillRect(0, 108, 240, 58);

  ctx.fillStyle = "rgb(255 255 255 / 18%)";
  ctx.beginPath();
  ctx.ellipse(122, 137, 92, 20, 0, 0, Math.PI * 2);
  ctx.fill();
}

function drawSignal() {
  clearScene("#c9eaf2");
  drawGround();
  roundedRect(91, 42, 58, 76, 22, "rgb(255 253 247 / 86%)");

  ctx.fillStyle = "#286e58";
  ctx.beginPath();
  ctx.arc(120, 78, 12, 0, Math.PI * 2);
  ctx.fill();
  ctx.beginPath();
  ctx.moveTo(109, 84);
  ctx.lineTo(120, 102);
  ctx.lineTo(131, 84);
  ctx.closePath();
  ctx.fill();

  ctx.fillStyle = "#fffdf7";
  ctx.beginPath();
  ctx.arc(120, 78, 4, 0, Math.PI * 2);
  ctx.fill();

  const strength = state === "scanning" ? 3 : 2;
  for (let i = 0; i < strength; i += 1) {
    ctx.strokeStyle = i === strength - 1 ? "#ef6658" : "rgb(42 122 90 / 55%)";
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.arc(120, 78, 22 + i * 12, Math.PI * 1.13, Math.PI * 1.87);
    ctx.stroke();
  }
}

function drawCharmanderIllustration(scale = 1, offsetX = 0, offsetY = 0) {
  const outline = "#2a211d";
  const orange = "#e98342";
  const orangeLight = "#f3a464";
  const belly = "#f8dfac";
  const teal = "#16758a";

  ctx.save();
  ctx.translate(offsetX, offsetY);
  ctx.scale(scale, scale);
  ctx.lineCap = "round";
  ctx.lineJoin = "round";

  // Tail and flame sit behind the body.
  ctx.beginPath();
  ctx.moveTo(34, 43);
  ctx.bezierCurveTo(47, 47, 57, 38, 58, 23);
  ctx.strokeStyle = outline;
  ctx.lineWidth = 8;
  ctx.stroke();
  ctx.strokeStyle = orange;
  ctx.lineWidth = 5;
  ctx.stroke();

  ctx.fillStyle = "#d83d2f";
  ctx.strokeStyle = outline;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(58, 25);
  ctx.lineTo(53, 17);
  ctx.lineTo(57, 18);
  ctx.lineTo(58, 7);
  ctx.lineTo(62, 13);
  ctx.lineTo(66, 8);
  ctx.lineTo(65, 18);
  ctx.lineTo(69, 16);
  ctx.lineTo(66, 25);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.fillStyle = "#ffcf45";
  ctx.beginPath();
  ctx.moveTo(59, 24);
  ctx.lineTo(57, 18);
  ctx.lineTo(60, 19);
  ctx.lineTo(61, 13);
  ctx.lineTo(64, 19);
  ctx.lineTo(66, 18);
  ctx.lineTo(64, 24);
  ctx.closePath();
  ctx.fill();

  // Feet and arms establish the familiar wide, friendly stance.
  ctx.fillStyle = orange;
  ctx.strokeStyle = outline;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.ellipse(17, 48, 10, 8, -0.2, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
  ctx.beginPath();
  ctx.ellipse(36, 49, 10, 8, 0.2, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  ctx.beginPath();
  ctx.moveTo(15, 29);
  ctx.lineTo(4, 34);
  ctx.lineTo(8, 37);
  ctx.lineTo(3, 39);
  ctx.lineTo(16, 38);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(35, 29);
  ctx.lineTo(49, 34);
  ctx.lineTo(45, 37);
  ctx.lineTo(51, 39);
  ctx.lineTo(35, 38);
  ctx.closePath();
  ctx.fill();
  ctx.stroke();

  // Torso and cream belly.
  ctx.beginPath();
  ctx.ellipse(26, 36, 14, 18, 0, 0, Math.PI * 2);
  ctx.fillStyle = orange;
  ctx.fill();
  ctx.stroke();
  ctx.beginPath();
  ctx.ellipse(26, 39, 9, 14, 0, 0, Math.PI * 2);
  ctx.fillStyle = belly;
  ctx.fill();
  ctx.stroke();

  // Oversized head, eyes and open smile carry the character likeness.
  ctx.beginPath();
  ctx.ellipse(25, 15, 18, 15, 0, 0, Math.PI * 2);
  ctx.fillStyle = orangeLight;
  ctx.fill();
  ctx.stroke();

  for (const eyeX of [17, 31]) {
    ctx.beginPath();
    ctx.ellipse(eyeX, 12, 4, 7, 0, 0, Math.PI * 2);
    ctx.fillStyle = outline;
    ctx.fill();
    ctx.beginPath();
    ctx.ellipse(eyeX, 14, 2.7, 4.5, 0, 0, Math.PI * 2);
    ctx.fillStyle = teal;
    ctx.fill();
    ctx.beginPath();
    ctx.ellipse(eyeX - 1, 9, 1.5, 2.5, 0, 0, Math.PI * 2);
    ctx.fillStyle = "#ffffff";
    ctx.fill();
  }

  ctx.fillStyle = outline;
  ctx.fillRect(23, 17, 1.5, 1.5);
  ctx.fillRect(28, 17, 1.5, 1.5);
  ctx.beginPath();
  ctx.moveTo(13, 20);
  ctx.quadraticCurveTo(25, 30, 38, 20);
  ctx.quadraticCurveTo(26, 35, 13, 20);
  ctx.closePath();
  ctx.fillStyle = "#583743";
  ctx.fill();
  ctx.strokeStyle = outline;
  ctx.stroke();
  ctx.beginPath();
  ctx.moveTo(19, 27);
  ctx.quadraticCurveTo(26, 31, 33, 26);
  ctx.strokeStyle = "#d98696";
  ctx.lineWidth = 3;
  ctx.stroke();

  ctx.fillStyle = "#ffffff";
  ctx.beginPath();
  ctx.moveTo(16, 22);
  ctx.lineTo(20, 23);
  ctx.lineTo(18, 26);
  ctx.closePath();
  ctx.fill();
  ctx.beginPath();
  ctx.moveTo(34, 23);
  ctx.lineTo(38, 21);
  ctx.lineTo(36, 26);
  ctx.closePath();
  ctx.fill();

  // Three white claws on each foot.
  for (const claw of [
    [9, 52, 13, 49],
    [14, 54, 17, 50],
    [34, 52, 37, 49],
    [39, 53, 41, 49],
  ]) {
    ctx.beginPath();
    ctx.moveTo(claw[0], claw[1]);
    ctx.lineTo(claw[2], claw[3]);
    ctx.lineTo(claw[2] + 1, claw[1] + 1);
    ctx.closePath();
    ctx.fillStyle = "#ffffff";
    ctx.fill();
    ctx.strokeStyle = outline;
    ctx.lineWidth = 1;
    ctx.stroke();
  }

  ctx.restore();
}

function drawCharmander(scale = 1, offsetX = 0, offsetY = 0) {
  const size = 68 * scale;
  if (charmanderImage.complete && charmanderImage.naturalWidth > 0) {
    ctx.drawImage(charmanderImage, offsetX, offsetY, size, size);
    return;
  }
  drawCharmanderIllustration(scale, offsetX, offsetY);
}

function drawPokeball(x, y, scale = 1) {
  const size = 18 * scale;
  const radius = size / 2;
  ctx.save();
  ctx.translate(x, y);
  ctx.shadowColor = "rgb(25 32 24 / 35%)";
  ctx.shadowBlur = 2 * scale;
  ctx.shadowOffsetY = scale;

  const shell = ctx.createRadialGradient(
    size * 0.32,
    size * 0.24,
    1,
    radius,
    radius,
    radius,
  );
  shell.addColorStop(0, "#ffffff");
  shell.addColorStop(1, "#d8e0df");
  ctx.fillStyle = shell;
  ctx.beginPath();
  ctx.arc(radius, radius, radius - 1, 0, Math.PI * 2);
  ctx.fill();

  ctx.shadowColor = "transparent";
  ctx.save();
  ctx.beginPath();
  ctx.arc(radius, radius, radius - 1, 0, Math.PI * 2);
  ctx.clip();

  const redShell = ctx.createLinearGradient(0, 0, 0, radius);
  redShell.addColorStop(0, "#ff8173");
  redShell.addColorStop(0.58, "#ef5048");
  redShell.addColorStop(1, "#c93235");
  ctx.fillStyle = redShell;
  ctx.fillRect(0, 0, size, radius + 1);

  ctx.fillStyle = "#28383a";
  ctx.fillRect(0, radius - 2 * scale, size, 4 * scale);
  ctx.restore();

  ctx.strokeStyle = "rgb(24 50 56 / 45%)";
  ctx.lineWidth = Math.max(1, scale * 0.7);
  ctx.beginPath();
  ctx.arc(radius, radius, radius - 1, 0, Math.PI * 2);
  ctx.stroke();

  ctx.fillStyle = "#28383a";
  ctx.beginPath();
  ctx.arc(radius, radius, 4.2 * scale, 0, Math.PI * 2);
  ctx.fill();
  ctx.fillStyle = "#ffffff";
  ctx.beginPath();
  ctx.arc(radius, radius, 2.6 * scale, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "rgb(255 255 255 / 62%)";
  ctx.beginPath();
  ctx.ellipse(size * 0.34, size * 0.24, size * 0.12, size * 0.07, -0.4, 0, Math.PI * 2);
  ctx.fill();
  ctx.restore();
}

function drawEncounter() {
  clearScene();
  drawGround();
  ctx.fillStyle = "rgb(28 76 57 / 20%)";
  ctx.beginPath();
  ctx.ellipse(120, 132, 49, 9, 0, 0, Math.PI * 2);
  ctx.fill();
  drawCharmander(1.92, 55, 18);
}

function drawCapture() {
  clearScene();
  drawGround();
  ctx.fillStyle = "rgb(28 76 57 / 20%)";
  ctx.beginPath();
  ctx.ellipse(120, 112, 40, 7, 0, 0, Math.PI * 2);
  ctx.fill();
  drawCharmander(1.52, 68, 9);

  const inTarget = markerValue >= TARGET_START && markerValue <= TARGET_END;
  const ringRadius = 31 + Math.abs(markerValue - 50) * 0.28;
  ctx.strokeStyle = "rgb(255 255 255 / 82%)";
  ctx.lineWidth = 7;
  ctx.beginPath();
  ctx.arc(120, 62, ringRadius, 0, Math.PI * 2);
  ctx.stroke();
  ctx.strokeStyle = inTarget ? "#55b96d" : "#ef6658";
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.arc(120, 62, ringRadius, 0, Math.PI * 2);
  ctx.stroke();

  ctx.strokeStyle = "rgb(255 255 255 / 55%)";
  ctx.lineWidth = 4;
  ctx.beginPath();
  ctx.moveTo(120, 162);
  ctx.quadraticCurveTo(84, 83, 117, 73);
  ctx.stroke();
  drawPokeball(87, 128, 3.7);
}

function drawThrowing(now = performance.now()) {
  clearScene();
  drawGround();
  ctx.fillStyle = "rgb(28 76 57 / 20%)";
  ctx.beginPath();
  ctx.ellipse(120, 112, 40, 7, 0, 0, Math.PI * 2);
  ctx.fill();
  drawCharmander(1.52, 68, 9);

  const duration = reducedMotion ? 140 : THROW_DURATION_MS;
  const progress = Math.min(1, Math.max(0, (now - throwStartedAt) / duration));
  const eased = 1 - ((1 - progress) ** 3);
  const inverse = 1 - eased;
  const ballX = inverse * inverse * 120 + 2 * inverse * eased * 78 + eased * eased * 120;
  const ballY = inverse * inverse * 156 + 2 * inverse * eased * 43 + eased * eased * 66;
  const ballScale = 3.7 - eased * 2.55;

  ctx.save();
  ctx.globalAlpha = 0.18;
  ctx.strokeStyle = "#ffffff";
  ctx.lineWidth = 7;
  ctx.beginPath();
  ctx.moveTo(120, 158);
  ctx.quadraticCurveTo(78, 43, 120, 66);
  ctx.stroke();
  ctx.restore();

  drawPokeball(
    ballX - (18 * ballScale) / 2,
    ballY - (18 * ballScale) / 2,
    ballScale,
  );
}

function drawCaptured(now = performance.now()) {
  clearScene();
  drawGround();
  const elapsed = Math.max(0, now - catchStartedAt);
  const activeWobble = state === "catching" && !reducedMotion;
  const wobble = activeWobble ? Math.sin(elapsed / 58) * Math.max(0, 5 - elapsed / 250) : 0;

  ctx.fillStyle = "rgb(28 76 57 / 24%)";
  ctx.beginPath();
  ctx.ellipse(120, 127, 40, 8, 0, 0, Math.PI * 2);
  ctx.fill();
  drawPokeball(93 + wobble, 67, 3);

  if (state === "captured") {
    ctx.fillStyle = "#ffd45f";
    for (const [x, y, size] of [
      [76, 58, 5],
      [163, 64, 4],
      [78, 100, 3],
      [164, 105, 5],
      [121, 45, 4],
    ]) {
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(Math.PI / 4);
      ctx.fillRect(-size / 2, -size / 2, size, size);
      ctx.restore();
    }
  }
}

function drawHome() {
  clearScene("#fffdf7");
  const rows = [
    ["探索", "寻找附近的精灵"],
    ["图鉴", save.discoveryState === "unknown"
      ? "0 已发现 · 0 已捕获"
      : save.discoveryState === "seen"
        ? "1 已发现 · 0 已捕获"
        : "1 已发现 · 1 已捕获"],
  ];
  rows.forEach(([label, detail], index) => {
    const y = 10 + index * 76;
    const selected = homeSelection === index;
    roundedRect(8, y, 224, 66, 8, selected ? "#e4f4e8" : "#f7fbf8");
    ctx.strokeStyle = selected ? "#55b96d" : "#d5e0dd";
    ctx.lineWidth = selected ? 3 : 1;
    ctx.strokeRect(9, y + 1, 222, 64);
    ctx.fillStyle = "#183238";
    ctx.font = "800 18px system-ui, sans-serif";
    ctx.fillText(label, 24, y + 28);
    ctx.fillStyle = "#60777a";
    ctx.font = "600 10px system-ui, sans-serif";
    ctx.fillText(detail, 24, y + 50);
    if (selected) {
      ctx.fillStyle = "#ef5b4f";
      ctx.font = "800 20px system-ui, sans-serif";
      ctx.fillText(">", 205, y + 38);
    }
  });
}

function drawBestiaryList() {
  clearScene("#fffdf7");
  if (save.discoveryState === "unknown") {
    roundedRect(8, 10, 224, 146, 8, "#f7fbf8");
    ctx.fillStyle = "#ef5b4f";
    ctx.font = "800 26px system-ui, sans-serif";
    ctx.fillText("?", 112, 57);
    ctx.fillStyle = "#183238";
    ctx.font = "800 16px system-ui, sans-serif";
    ctx.fillText("还没有图鉴条目", 64, 91);
    ctx.fillStyle = "#60777a";
    ctx.font = "600 11px system-ui, sans-serif";
    ctx.fillText("先去探索一个地点", 70, 116);
    return;
  }

  const selected = bestiarySelection === 0;
  roundedRect(8, 8, 224, 94, 8, selected ? "#e4f4e8" : "#f7fbf8");
  ctx.save();
  if (save.discoveryState === "seen") ctx.globalAlpha = 0.55;
  drawCharmander(0.76, 9, 13);
  ctx.restore();
  ctx.fillStyle = "#183238";
  ctx.font = "800 13px system-ui, sans-serif";
  ctx.fillText("No.004 小火龙", 94, 34);
  ctx.fillStyle = save.discoveryState === "captured" ? "#ef5b4f" : "#55a86c";
  ctx.font = "800 11px system-ui, sans-serif";
  ctx.fillText(save.discoveryState === "captured" ? "已捕获" : "已发现", 94, 58);
  ctx.fillStyle = "#60777a";
  ctx.font = "600 10px system-ui, sans-serif";
  ctx.fillText("捕获 " + save.captureCount, 94, 80);

  const backSelected = bestiarySelection === 1;
  roundedRect(8, 112, 224, 44, 8, backSelected ? "#e4f4e8" : "#f7fbf8");
  ctx.fillStyle = "#183238";
  ctx.font = "800 14px system-ui, sans-serif";
  ctx.fillText("返回首页", 24, 140);
}

function drawBestiary() {
  clearScene("#ffe4c4");

  ctx.fillStyle = "#ef7054";
  ctx.beginPath();
  ctx.moveTo(0, 0);
  ctx.lineTo(105, 0);
  ctx.quadraticCurveTo(86, 70, 112, 166);
  ctx.lineTo(0, 166);
  ctx.closePath();
  ctx.fill();

  roundedRect(8, 10, 224, 146, 12, "rgb(255 253 247 / 88%)");
  ctx.fillStyle = "rgb(229 91 65 / 14%)";
  ctx.beginPath();
  ctx.ellipse(60, 127, 43, 9, 0, 0, Math.PI * 2);
  ctx.fill();
  ctx.save();
  if (save.discoveryState === "seen") ctx.globalAlpha = 0.55;
  drawCharmander(1.48, 8, 37);
  ctx.restore();

  ctx.fillStyle = "#657b7d";
  ctx.font = "700 9px system-ui, sans-serif";
  ctx.fillText("No.004", 124, 35);
  ctx.fillStyle = "#183238";
  ctx.font = "800 18px system-ui, sans-serif";
  ctx.fillText("小火龙", 124, 58);

  roundedRect(124, 67, 48, 18, 8,
    save.discoveryState === "captured" ? "#ef6658" : "#55a86c");
  ctx.fillStyle = "#ffffff";
  ctx.font = "800 9px system-ui, sans-serif";
  ctx.fillText(save.discoveryState === "captured" ? "捕获" : "发现", 136, 80);

  ctx.fillStyle = "#60777a";
  ctx.font = "600 9px system-ui, sans-serif";
  ctx.fillText("捕获次数  " + save.captureCount, 124, 105);
  ctx.fillText(save.placeId ? "发现地点  城市绿地" : "发现地点  --", 124, 122);

  ctx.fillStyle = "#d7e8e4";
  ctx.fillRect(124, 136, 86, 6);
  ctx.fillStyle = "#4e9d71";
  ctx.fillRect(124, 136, 29, 6);
}

function updateTrace() {
  const order = ["place", "encounter", "capture", "bestiary"];
  const activeByState = {
    home: "place",
    scanning: "place",
    place: "place",
    encounter: "encounter",
    capture: "capture",
    throwing: "capture",
    catching: "capture",
    captured: "capture",
    storageError: "capture",
    escaped: "capture",
    bestiaryList: "bestiary",
    bestiaryDetail: "bestiary",
  };
  const active = activeByState[state];
  const activeIndex = order.indexOf(active);

  document.querySelectorAll(".trace-list li").forEach((item) => {
    const index = order.indexOf(item.dataset.step);
    item.classList.toggle("active", index === activeIndex);
    item.classList.toggle("done", index < activeIndex);
  });
}

function render() {
  device.dataset.state = state;
  screen.dataset.state = state;
  captureCount.textContent = String(save.captureCount);
  captureMeter.hidden = state !== "capture";
  saveState.textContent = save.discoveryState === "captured"
    ? "图鉴已捕获"
    : save.discoveryState === "seen"
      ? "图鉴已发现"
      : "离线模式";
  const navigable = state === "home" ||
    (state === "bestiaryList" && save.discoveryState !== "unknown");
  upButton.disabled = !navigable;
  downButton.disabled = !navigable;

  if (state === "home") {
    eyebrow.textContent = "离线图鉴";
    title.textContent = "城市精灵";
    message.textContent = homeSelection === 0 ? "探索附近地点" : "查看收藏记录";
    meta.textContent = "使用上下键选择";
    screenAction.textContent = "上下选择 · OK 打开";
    drawHome();
  } else if (state === "scanning" || state === "place") {
    eyebrow.textContent = "城市绿地";
    title.textContent = state === "place" ? "地点确认成功" : "发现附近的气息";
    message.textContent =
      state === "scanning" ? "正在感知周围环境..." :
      state === "place" ? "这里似乎有精灵出没" : "开始探索这个地点";
    meta.textContent =
      state === "scanning" ? "正在确认地点" :
      state === "place" ? "环境信号稳定" : "按下确认键扫描";
    screenAction.textContent = "正在扫描";
    drawSignal();
  } else if (state === "encounter") {
    eyebrow.textContent = "野外遭遇";
    title.textContent = "野生的小火龙出现了";
    message.textContent = "尾焰在风里轻轻跳动";
    meta.textContent = "No.004 · 已写入发现记录";
    screenAction.textContent = "准备捕获";
    drawEncounter();
  } else if (state === "capture") {
    eyebrow.textContent = `精灵球 × ${attempts}`;
    title.textContent = "瞄准小火龙";
    message.textContent = "捕获环变绿时投球";
    meta.textContent = "按下确认键投出精灵球";
    screenAction.textContent = "投球";
    drawCapture();
  } else if (state === "throwing") {
    eyebrow.textContent = "第一人称投掷";
    title.textContent = "精灵球飞出去了";
    message.textContent = "命中小火龙";
    meta.textContent = "正在判定捕获结果";
    screenAction.textContent = "投掷中";
    drawThrowing();
  } else if (state === "catching") {
    eyebrow.textContent = "捕获中";
    title.textContent = "精灵球命中了";
    message.textContent = "1 · 2 · 3";
    meta.textContent = "正在保存捕获记录";
    screenAction.textContent = "捕获中";
    drawCaptured();
  } else if (state === "captured") {
    eyebrow.textContent = "捕获成功";
    title.textContent = "太棒了！";
    message.textContent = "成功捕获小火龙";
    meta.textContent = `第 ${save.captureCount} 次捕获记录`;
    screenAction.textContent = "查看图鉴";
    drawCaptured();
  } else if (state === "storageError") {
    eyebrow.textContent = "保存失败";
    title.textContent = "记录尚未保存";
    message.textContent = pendingWrite === "discovery"
      ? "发现不会在保存前显示"
      : "没有增加捕获次数";
    meta.textContent = "修复存储后重试";
    screenAction.textContent = "重试保存";
    pendingWrite === "discovery" ? drawSignal() : drawCaptured();
  } else if (state === "escaped") {
    eyebrow.textContent = "捕获失败";
    title.textContent = "小火龙逃走了";
    message.textContent = "发现记录仍在图鉴中";
    meta.textContent = "回首页可查看图鉴";
    screenAction.textContent = "返回首页";
    drawEncounter();
  } else if (state === "bestiaryList") {
    eyebrow.textContent = "本地图鉴";
    title.textContent = "收藏记录";
    message.textContent = save.discoveryState === "unknown"
      ? "还没有发现精灵"
      : bestiarySelection === 0
        ? "No.004 · 小火龙"
        : "返回首页";
    meta.textContent = "已发现 " +
      (save.discoveryState === "unknown" ? 0 : 1) + " / 1";
    screenAction.textContent = save.discoveryState === "unknown"
      ? "OK 返回"
      : "上下选择 · OK 打开";
    drawBestiaryList();
  } else if (state === "bestiaryDetail") {
    eyebrow.textContent = "图鉴 No.004";
    title.textContent = "小火龙";
    message.textContent = save.discoveryState === "captured"
      ? "已捕获"
      : "已发现 · 尚未捕获";
    meta.textContent = "尾巴上的火焰象征生命力";
    screenAction.textContent = "返回列表";
    drawBestiary();
  }
  updateTrace();
}

function animateCapture(now) {
  if (state !== "capture") {
    return;
  }
  markerValue = markerAt(now);
  captureMarker.style.left = `${markerValue}%`;
  drawCapture();
  animationFrame = window.requestAnimationFrame(animateCapture);
}

function animateCatching(now) {
  if (state !== "catching") {
    return;
  }
  drawCaptured(now);
  animationFrame = window.requestAnimationFrame(animateCatching);
}

function animateThrow(now) {
  if (state !== "throwing") {
    return;
  }
  drawThrowing(now);
  animationFrame = window.requestAnimationFrame(animateThrow);
}

function markerAt(now) {
  const phase = ((now - captureStartedAt) % CAPTURE_CYCLE_MS) / CAPTURE_CYCLE_MS;
  return phase < 0.5 ? phase * 200 : (1 - phase) * 200;
}

function startCapture() {
  state = "capture";
  captureStartedAt = performance.now();
  render();
  animationFrame = window.requestAnimationFrame(animateCapture);
}

function finishThrow() {
  if (throwWillSucceed) {
    state = "catching";
    catchStartedAt = performance.now();
    render();
    if (!reducedMotion) {
      animationFrame = window.requestAnimationFrame(animateCatching);
    }
    pendingWrite = "capture";
    schedule(() => {
      if (persistCapture()) {
        pendingWrite = null;
        state = "captured";
      } else {
        state = "storageError";
      }
      render();
    }, reducedMotion ? 350 : 1300);
    return;
  }

  attempts -= 1;
  if (attempts === 0) {
    state = "escaped";
    render();
    return;
  }

  state = "capture";
  captureStartedAt = performance.now();
  render();
  message.textContent = "差一点，再瞄准一次";
  animationFrame = window.requestAnimationFrame(animateCapture);
}

function throwBall() {
  window.cancelAnimationFrame(animationFrame);
  markerValue = markerAt(performance.now());
  throwWillSucceed = markerValue >= TARGET_START && markerValue <= TARGET_END;
  throwStartedAt = performance.now();
  state = "throwing";
  render();
  if (!reducedMotion) {
    animationFrame = window.requestAnimationFrame(animateThrow);
  }
  schedule(finishThrow, reducedMotion ? 140 : THROW_DURATION_MS);
}

function handleOk() {
  if (state === "home") {
    if (homeSelection === 1) {
      bestiarySelection = 0;
      state = "bestiaryList";
      render();
      return;
    }
    state = "scanning";
    render();
    schedule(() => {
      state = "place";
      render();
    }, 650);
    schedule(() => {
      pendingWrite = "discovery";
      if (persistSeen()) {
        pendingWrite = null;
        attempts = 3;
        state = "encounter";
      } else {
        state = "storageError";
      }
      render();
    }, 1350);
  } else if (state === "encounter") {
    startCapture();
  } else if (state === "capture") {
    throwBall();
  } else if (state === "captured") {
    state = "bestiaryDetail";
    render();
  } else if (state === "storageError") {
    const operation = pendingWrite;
    const saved = operation === "discovery" ? persistSeen() : persistCapture();
    if (saved) {
      pendingWrite = null;
      state = operation === "discovery" ? "encounter" : "captured";
    }
    render();
  } else if (state === "escaped") {
    state = "home";
    render();
  } else if (state === "bestiaryList") {
    if (save.discoveryState === "unknown" || bestiarySelection === 1) {
      state = "home";
    } else {
      state = "bestiaryDetail";
    }
    render();
  } else if (state === "bestiaryDetail") {
    bestiarySelection = 0;
    state = "bestiaryList";
    render();
  }
}

function reset() {
  clearTimers();
  localStorage.removeItem(STORAGE_KEY);
  save = loadSave();
  attempts = 3;
  markerValue = 0;
  throwStartedAt = 0;
  throwWillSucceed = false;
  catchStartedAt = 0;
  captureMarker.style.left = "0%";
  state = "home";
  homeSelection = 0;
  bestiarySelection = 0;
  pendingWrite = null;
  render();
}

function handleDirection() {
  if (state === "home") {
    homeSelection = homeSelection === 0 ? 1 : 0;
    render();
  } else if (state === "bestiaryList" &&
             save.discoveryState !== "unknown") {
    bestiarySelection = bestiarySelection === 0 ? 1 : 0;
    render();
  }
}

okButton.addEventListener("click", handleOk);
upButton.addEventListener("click", handleDirection);
downButton.addEventListener("click", handleDirection);
resetButton.addEventListener("click", reset);
window.addEventListener("keydown", (event) => {
  if (event.key === "ArrowUp" || event.key === "ArrowDown") {
    event.preventDefault();
    handleDirection();
  } else if (event.key === "Enter" || event.key === " ") {
    if (event.target instanceof HTMLButtonElement) {
      return;
    }
    event.preventDefault();
    handleOk();
  }
});

window.citySpiritsMvp = {
  getState: () => ({
    state,
    attempts,
    markerValue,
    captureCount: save.captureCount,
    discoveryState: save.discoveryState,
    homeSelection,
    bestiarySelection,
  }),
};

charmanderImage.addEventListener("load", render);
render();
