/*
 * ndi_sender.cc
 *
 * A minimal Node.js (N-API) native addon that wraps the NewTek/NDI(tm) SDK
 * sender API. It receives raw BGRA frame buffers from the Electron renderer
 * and transmits them on the network as an NDI video source.
 *
 * Frames are double-buffered: NDIlib_send_send_video_async_v2() keeps a
 * pointer to the submitted buffer until the *next* async send, so we alternate
 * between two internally owned buffers to guarantee the in-flight frame is
 * never overwritten while it is still being transmitted.
 */

#include <napi.h>
#include <Processing.NDI.Lib.h>

#include <vector>
#include <cstring>
#include <string>

class NdiSender : public Napi::ObjectWrap<NdiSender> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  explicit NdiSender(const Napi::CallbackInfo& info);
  ~NdiSender();

 private:
  Napi::Value Send(const Napi::CallbackInfo& info);
  void Destroy(const Napi::CallbackInfo& info);
  void CleanUp();

  NDIlib_send_instance_t send_instance_ = nullptr;
  std::vector<uint8_t> buffers_[2];
  int buf_index_ = 0;
};

Napi::Object NdiSender::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "NdiSender", {
      InstanceMethod("send", &NdiSender::Send),
      InstanceMethod("destroy", &NdiSender::Destroy),
  });

  // Keep a persistent reference to the constructor.
  auto* constructor = new Napi::FunctionReference();
  *constructor = Napi::Persistent(func);
  env.SetInstanceData(constructor);

  exports.Set("NdiSender", func);
  return exports;
}

NdiSender::NdiSender(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<NdiSender>(info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    throw Napi::TypeError::New(env, "NdiSender requires a source name string");
  }

  std::string source_name = info[0].As<Napi::String>().Utf8Value();

  if (!NDIlib_initialize()) {
    throw Napi::Error::New(
        env, "NDIlib_initialize() failed. Is the NDI runtime installed and is "
             "the CPU supported?");
  }

  NDIlib_send_create_t create_desc;
  create_desc.p_ndi_name = source_name.c_str();
  create_desc.p_groups = nullptr;
  create_desc.clock_video = false;  // Electron's frame rate drives timing.
  create_desc.clock_audio = false;

  send_instance_ = NDIlib_send_create(&create_desc);
  if (!send_instance_) {
    throw Napi::Error::New(env, "NDIlib_send_create() failed");
  }
}

NdiSender::~NdiSender() { CleanUp(); }

void NdiSender::CleanUp() {
  if (send_instance_) {
    // Flush any in-flight async frame before destroying.
    NDIlib_send_send_video_async_v2(send_instance_, nullptr);
    NDIlib_send_destroy(send_instance_);
    send_instance_ = nullptr;
  }
}

Napi::Value NdiSender::Send(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!send_instance_) {
    throw Napi::Error::New(env, "send() called on a destroyed NdiSender");
  }

  if (info.Length() < 5 || !info[0].IsBuffer() || !info[1].IsNumber() ||
      !info[2].IsNumber() || !info[3].IsNumber() || !info[4].IsNumber()) {
    throw Napi::TypeError::New(
        env, "send(buffer, width, height, frameRateN, frameRateD) expected");
  }

  Napi::Buffer<uint8_t> incoming = info[0].As<Napi::Buffer<uint8_t>>();
  const int width = info[1].As<Napi::Number>().Int32Value();
  const int height = info[2].As<Napi::Number>().Int32Value();
  const int frame_rate_n = info[3].As<Napi::Number>().Int32Value();
  const int frame_rate_d = info[4].As<Napi::Number>().Int32Value();

  if (width <= 0 || height <= 0) {
    throw Napi::RangeError::New(env, "width and height must be positive");
  }

  const size_t expected = static_cast<size_t>(width) * height * 4;
  if (incoming.Length() < expected) {
    throw Napi::RangeError::New(
        env, "buffer is smaller than width * height * 4 (BGRA)");
  }

  // Copy into the next internal buffer (double buffering for async send).
  std::vector<uint8_t>& dst = buffers_[buf_index_];
  if (dst.size() != expected) {
    dst.resize(expected);
  }
  std::memcpy(dst.data(), incoming.Data(), expected);

  NDIlib_video_frame_v2_t frame;
  frame.xres = width;
  frame.yres = height;
  frame.FourCC = NDIlib_FourCC_type_BGRA;
  frame.frame_rate_N = frame_rate_n > 0 ? frame_rate_n : 60000;
  frame.frame_rate_D = frame_rate_d > 0 ? frame_rate_d : 1000;
  frame.picture_aspect_ratio = static_cast<float>(width) / static_cast<float>(height);
  frame.frame_format_type = NDIlib_frame_format_type_progressive;
  frame.timecode = NDIlib_send_timecode_synthesize;
  frame.p_data = dst.data();
  frame.line_stride_in_bytes = width * 4;
  frame.p_metadata = nullptr;
  frame.timestamp = 0;

  NDIlib_send_send_video_async_v2(send_instance_, &frame);

  // Flip buffers so the in-flight frame is not overwritten next time.
  buf_index_ ^= 1;

  return env.Undefined();
}

void NdiSender::Destroy(const Napi::CallbackInfo& info) { CleanUp(); }

static Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return NdiSender::Init(env, exports);
}

NODE_API_MODULE(ndi_sender, InitAll)
// ndi_sender.cc
//
// Minimal Node-API (N-API) native addon that wraps the NewTek/NDI 6 SDK
// "send" API. It receives BGRA pixel buffers from the Electron renderer and
// transmits them on the network as an NDI video source.
//
// Frames are sent asynchronously (NDIlib_send_send_video_async_v2) for low
// latency. Because the async API keeps a reference to the supplied buffer
// until the *next* async send, we use double buffering so the buffer that is
// currently "in flight" is never overwritten.

#include <napi.h>
#include <vector>
#include <cstring>

#include "Processing.NDI.Lib.h"

namespace {
bool g_ndi_initialized = false;
}

class NdiSender : public Napi::ObjectWrap<NdiSender> {
 public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  explicit NdiSender(const Napi::CallbackInfo& info);
  ~NdiSender();

 private:
  Napi::Value Send(const Napi::CallbackInfo& info);
  void Destroy(const Napi::CallbackInfo& info);
  void Cleanup();

  NDIlib_send_instance_t send_instance_ = nullptr;
  std::vector<uint8_t> buffers_[2];
  int buf_index_ = 0;
};

Napi::Object NdiSender::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(env, "NdiSender", {
    InstanceMethod("send", &NdiSender::Send),
    InstanceMethod("destroy", &NdiSender::Destroy),
  });

  exports.Set("NdiSender", func);
  return exports;
}

NdiSender::NdiSender(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<NdiSender>(info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "NdiSender requires a source name (string)")
        .ThrowAsJavaScriptException();
    return;
  }

  if (!g_ndi_initialized) {
    if (!NDIlib_initialize()) {
      Napi::Error::New(env,
          "Failed to initialize the NDI runtime. Ensure the NDI 6 runtime "
          "is installed and Processing.NDI.Lib.x64.dll is reachable.")
          .ThrowAsJavaScriptException();
      return;
    }
    g_ndi_initialized = true;
  }

  std::string name = info[0].As<Napi::String>().Utf8Value();

  NDIlib_send_create_t create_desc;
  create_desc.p_ndi_name = name.c_str();
  create_desc.p_groups = nullptr;
  // We drive timing from the Electron frame rate, so let NDI deliver frames
  // as soon as they arrive instead of clocking to the declared frame rate.
  create_desc.clock_video = false;
  create_desc.clock_audio = false;

  send_instance_ = NDIlib_send_create(&create_desc);
  if (!send_instance_) {
    Napi::Error::New(env, "NDIlib_send_create returned null")
        .ThrowAsJavaScriptException();
    return;
  }
}

NdiSender::~NdiSender() {
  Cleanup();
}

void NdiSender::Cleanup() {
  if (send_instance_) {
    // Flush any in-flight async frame before destroying the sender.
    NDIlib_send_send_video_async_v2(send_instance_, nullptr);
    NDIlib_send_destroy(send_instance_);
    send_instance_ = nullptr;
  }
}

Napi::Value NdiSender::Send(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  if (!send_instance_) {
    Napi::Error::New(env, "Sender has been destroyed")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  if (info.Length() < 5 || !info[0].IsBuffer() || !info[1].IsNumber() ||
      !info[2].IsNumber() || !info[3].IsNumber() || !info[4].IsNumber()) {
    Napi::TypeError::New(env,
        "send(buffer, width, height, frameRateN, frameRateD) expected")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  Napi::Buffer<uint8_t> input = info[0].As<Napi::Buffer<uint8_t>>();
  const int width = info[1].As<Napi::Number>().Int32Value();
  const int height = info[2].As<Napi::Number>().Int32Value();
  const int frame_rate_n = info[3].As<Napi::Number>().Int32Value();
  const int frame_rate_d = info[4].As<Napi::Number>().Int32Value();

  if (width <= 0 || height <= 0) {
    Napi::RangeError::New(env, "width and height must be positive")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  const size_t expected = static_cast<size_t>(width) * height * 4;
  if (input.Length() < expected) {
    Napi::RangeError::New(env, "buffer smaller than width*height*4")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // Copy into a persistent, double-buffered backing store so the previously
  // queued async frame stays valid until NDI picks up this new one.
  std::vector<uint8_t>& dst = buffers_[buf_index_];
  if (dst.size() != expected) {
    dst.resize(expected);
  }
  std::memcpy(dst.data(), input.Data(), expected);

  NDIlib_video_frame_v2_t frame;
  frame.xres = width;
  frame.yres = height;
  frame.FourCC = NDIlib_FourCC_type_BGRA;
  frame.frame_rate_N = frame_rate_n > 0 ? frame_rate_n : 60000;
  frame.frame_rate_D = frame_rate_d > 0 ? frame_rate_d : 1000;
  frame.picture_aspect_ratio = static_cast<float>(width) / height;
  frame.frame_format_type = NDIlib_frame_format_type_progressive;
  frame.timecode = NDIlib_send_timecode_synthesize;
  frame.p_data = dst.data();
  frame.line_stride_in_bytes = width * 4;
  frame.p_metadata = nullptr;

  NDIlib_send_send_video_async_v2(send_instance_, &frame);

  // Alternate buffers for the next frame.
  buf_index_ ^= 1;

  return env.Undefined();
}

void NdiSender::Destroy(const Napi::CallbackInfo& info) {
  Cleanup();
}

static Napi::Object InitAll(Napi::Env env, Napi::Object exports) {
  return NdiSender::Init(env, exports);
}

NODE_API_MODULE(ndi_sender, InitAll)
