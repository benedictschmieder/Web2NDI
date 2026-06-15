// Simple file logger that also mirrors to the console.
//
// Writes to <userData>/logs/htmltondi.log so the packaged (windowless) app
// leaves a trace you can inspect from the tray menu when something fails.

const fs = require("fs");
const path = require("path");
const { app } = require("electron");
const { EventEmitter } = require("events");

let logFilePath = null;
let stream = null;

// In-memory ring buffer of recent lines plus an emitter, so a live log viewer
// window can show history on open and stream new lines as they arrive.
const MAX_BUFFER = 2000;
const buffer = [];
const events = new EventEmitter();
events.setMaxListeners(0);

function init() {
  if (logFilePath) return logFilePath;
  const dir = path.join(app.getPath("userData"), "logs");
  try {
    fs.mkdirSync(dir, { recursive: true });
  } catch (e) {
    /* ignore */
  }
  logFilePath = path.join(dir, "htmltondi.log");
  try {
    // Truncate on each launch so the file reflects the current session.
    stream = fs.createWriteStream(logFilePath, { flags: "w" });
  } catch (e) {
    stream = null;
  }
  return logFilePath;
}

function write(level, args) {
  const line = `${new Date().toISOString()} [${level}] ${args
    .map((a) => (typeof a === "string" ? a : JSON.stringify(a)))
    .join(" ")}`;
  if (stream) {
    try {
      stream.write(line + "\n");
    } catch (e) {
      /* ignore */
    }
  }
  buffer.push(line);
  if (buffer.length > MAX_BUFFER) buffer.shift();
  events.emit("line", line);
  return line;
}

// Patch console so existing console.* calls are captured to the file too.
function patchConsole() {
  const orig = {
    log: console.log.bind(console),
    warn: console.warn.bind(console),
    error: console.error.bind(console),
  };
  console.log = (...a) => {
    orig.log(write("info", a));
  };
  console.warn = (...a) => {
    orig.warn(write("warn", a));
  };
  console.error = (...a) => {
    orig.error(write("error", a));
  };
}

function getLogFilePath() {
  return logFilePath || init();
}

// Snapshot of the buffered session lines (oldest first).
function getBuffer() {
  return buffer.slice();
}

module.exports = { init, patchConsole, getLogFilePath, getBuffer, events };
