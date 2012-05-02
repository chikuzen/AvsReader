AviSynth Script Reader for AviUtl version 0.5.1

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
A: AviUtl0.99l以降用のAviSynthスクリプト/d2vファイル入力プラグインです。


Q: 既存のものとの違いは何ですか？
A: AviSynthスクリプトをVideo for Windowsを介さず、avisynth.dllを直接操作して読み込みます。
   また、DGIndexで作成されたd2vファイルをVFAPIを介さずに読み込みます。


Q: 具体的には？
A: AviUtlが対応していない色空間(YV12/YV16/YV24/YV411/Y8/RGB32)のクリップを読み込む場合、
   自動で YUY2()(YV16/YV12/YV411の場合) か RGB24()(RGB32の場合) に変換します。
   クリップがYV24/Y8の場合、内部でYC48(BT.601)変換を行います。
   また、音声サンプルが24bit/32bit/floatの場合、自動でConvertAudioTo16bit()がかかります。

   d2vファイルを読み込む場合は、MPEG2Source("*.d2v") と同様の処理が行われます。
   この機能を使うためには、DGDecode.dll または、MPEG2Dec3k.dll がAviSynthの
   オートローディングフォルダに置かれている必要があります。


Q: aviutl.exe と同じ場所に avsreader.ini というファイルが出来ましたが、これはなんですか？
A: このプラグイン用の設定ファイルです。
   後述のhighbit_depthとadjust_audio_length等の設定をこのファイルの内容で決定します。
   なお、avsreader.iniを削除したり、他の場所に移動させた場合、元の場所に自動的に新しいものが作られます。
   初期設定値はなるべく問題の起こらない無難なものにしてありますので、以下の説明がよく分からない方は
   何もいじらずそのまま使ったほうがよいでしょう。


Q: highbit_depthってなんですか？
A: dither hackへの対応です。
   avsreader.iniの highbit_depth=0 を highbit_depth=1 に書き換えると、dither hackのintealeaved formatに対応します。
   このモードはAviSynth2.6以降専用です。2.5xでは、これは無視されます。
   このモードを使用する際は、出力ビット深度16bitのみ対応しています。
   このモードにおける挙動は以下の通りです。

   入力クリップがYV24/Y8の場合          : 16bitYUV -> YC48変換を行います。
      〃         YV16の場合             : 16bitYUV -> YC48変換を行います。
　　　　　　　　　　　　　　　　　　　　　水平方向の色差は線形補間されます。
      〃         YV12の場合             : まず入力クリップにConvertToYV16()を掛けます。
                                          後の挙動はYV16の場合と同じです。
      〃         YV411の場合            : クリップを読み込みません。
      〃         YUY2/RGB24/RGB32の場合 : そのままYUY2/RGB24として読み込みます。

   16bitYUV->YC48の変換式は次のようになっています。
      YC48_Y  = ((16bit_y - 4096) * 4789) / 256
      YC48_Cb = ((16bit_u - 32768) * 4683) / 256
      YC48_Cr = ((16bit_v - 32768) * 4683) / 256
   いずれも符号付き32bit整数で計算した後、符号付き16bit整数にキャストしています。


Q: adjust_audio_lengthってなんですか？
A: adjust_audio_length=1 の状態で音声の長さが映像より長い/短いクリップを読み込むと、
   自動的に音声の長さが映像と合うようにサンプルの削除/無音の追加が行われます。
   (内部でTrim(0,0)を追加するだけです)
   adjust_audio_length=0 に書き換えれば、この処理は行われません。


Q: d2v_upconvってなんですか？
A: d2vファイルを読み込んでYV12->YUY2変換を行う際に、DGDecode.dllのupConvオプションを
   使用するかどうかを設定します(デフォルト: 1(使用する))。
   MPEG2Dec3kにはこのオプションはないので、DVD2AVI/MPEG2Dec3kを使用する場合はこれを
   d2v_upconv=0 に必ず書き換えて下さい。
   d2v_upconv=0 にすると、YUY2変換に次のyuy2converterで設定したものを使います。


Q: yuy2converterってなんですか？
A: YUY2変換を行う際に使用するフィルタを設定します。(デフォルト: ConvertToYUY2)
   例えば yuy2converter=ConvertToYUY2 を yuy2converter=AutoYUY2 に書き換えれば、
   YUY2変換をAutoYUY2()で行うようになります。
   (AviSynthのオートローディングフォルダにAutoYUY2.dllを入れておく必要があります)

   応用例:
   本プラグイン同梱のYUY2Converter.avsiとTIVTC.dllをオートローディングフォルダに置き、
   yuy2converter=ConditionalConvertToYUY2 と書き換えれば、このスクリプト関数が使用できます。
   ConditionalConvertToYUY2()は、(理論上は)DGDecodeのupConv=1よりも高品質、かつ、upConv=1や
   AutoYUY2()よりも高速なので、作者の個人的な意見としては一番のオススメです。
   なお、ここで設定する関数には引数を与えることは現状では出来ませんので、決め打ちでも大丈夫なものに
   して下さい。


Q: その他の注意事項は？
A: 初期設定ではYUY2変換はプログレッシブ前提になります。

   テストはAviSynth2.6でしか行なっていません。
   2.5xで不具合が起こったとしても、直す気もありません。
   もし、2.5xを使っていて問題があるようなら、とりあえず2.6に更新し、それでもダメだった場合のみ報告して下さい。


ソースコード:
https://github.com/chikuzen/AvsReader/
