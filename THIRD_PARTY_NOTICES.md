# Third-party software in these plug-ins

- **Steinberg VST 3 SDK** (MIT licence) — see `VST3_SDK_LICENSE.txt`.
  VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
- **VSTGUI** (BSD 3-clause licence) — see `VSTGUI_LICENSE.txt`.
- **dr_wav, dr_flac, dr_mp3** by David Reed (public domain / MIT-0) — https://github.com/mackron/dr_libs.
  dr_mp3 is based on minimp3 by lieff (CC0).
- **Faust libraries, `compressors.lib`, `co.xfer_ott`** by David Braun (MIT licence) —
  https://github.com/grame-cncm/faustlibraries/pull/257. Multidyn's OTT style
  (`plugins/multidyn/src/core/Ott.h`) uses its constants and laws: a fit to measurements of Xfer
  Records' OTT. Multidyn is not affiliated with or endorsed by Xfer Records.

  ```
  MIT License

  Copyright (c) David Braun

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
  ```
