AviSynth Script Reader for AviUtl version 0.0.2

Copyright (c) 2012 Oka Motofumi (chikuzen.mo at gmail dot com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.


Q: これは何ですか？
A: AviUtl用のAviSynthスクリプト入力プラグインです。

Q: 既存のものとの違いは何ですか？
A: Video for Windowsを介さず、avisynth.dllを直接操作してスクリプトを読み込みます。

Q: 具体的には？
A: AviUtlが対応していない色空間(YV12/YV16/YV24/YV411/Y8/RGB32)のクリップを読み込む場合、
   自動でConvertToYUY2()(YUVの場合)/ConvertToRGB24()(RGB32の場合)がかかります。
   また、音声サンプルが24bit/32bit/floatの場合、自動でConvertAudioTo16bit()がかかります。

Q: 注意することはありますか？
A: Y8/YV24のクリップで、widthが奇数の場合は、ConvertToYUY2がかけられないので読めません。


ソースコード:
https://github.com/chikuzen/AvsReader/