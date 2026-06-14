{
  "targets": [
    {
      "target_name": "ndi_sender",
      "sources": [ "native/ndi_sender.cc" ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": [ "NAPI_CPP_EXCEPTIONS" ],
      "conditions": [
        [ "OS=='win'", {
          "include_dirs": [
            "<!(node -e \"console.log((process.env.NDI_SDK_DIR || 'C:/Program Files/NDI/NDI 6 SDK') + '/Include')\")"
          ],
          "libraries": [
            "<!(node -e \"console.log((process.env.NDI_SDK_DIR || 'C:/Program Files/NDI/NDI 6 SDK') + '/Lib/x64/Processing.NDI.Lib.x64.lib')\")"
          ],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1,
              "AdditionalOptions": [ "/EHsc", "/std:c++17" ]
            }
          }
        } ]
      ]
    }
  ]
}
