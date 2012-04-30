AviSynth Script Reader for AviUtl version 0.2.1

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
   自動でConvertToYUY2()(YUV422/YUV411/YUV420の場合)/ConvertToRGB24()(RGB32の場合)がかかります。
   クリップがYV24/Y8の場合、内部でYC48(BT.601)変換を行います。
   また、音声サンプルが24bit/32bit/floatの場合、自動でConvertAudioTo16bit()がかかります。


Q: 横幅が奇数のYV24/Y8なクリップを読みこませると、横幅が1増えますがなぜですか？
   横幅が奇数なクリップをYC48でAviUtlに渡して色変換設定の入力をBT.709にすると、なぜか映像が崩壊します(原因不明)。
   これを避けるため、現状では幅が奇数の場合、右端の縦一列のピクセルをコピーして幅を偶数にするようにしています。
   どうしても幅を奇数にしたい場合は、"クリッピング&リサイズフィルタ"で右端を1ピクセル削って下さい。
   なお、入力がRGB24/RGB32の場合はこれは関係ありません。


Q: aviutl.exe と同じ場所に avsreader.ini というファイルが出来ましたが、これはなんですか？
A: 中身の highbit_depth=0 を highbit_depth=1 に書き換えると、dither hackのintealeaved formatに対応します。
   このモードはAviSynth2.6以降専用です。2.6未満のバージョンでは、これは無視されます。
   このモードを使用する際は、出力ビット深度16bitのみ対応しています。
   このモードにおける挙動は以下の通りです。

   入力クリップがYV24/Y8の場合          : 16bitYUV->YC48変換を行います。
      〃         YV16の場合             : 16bitYUV->YC48変換を行います。水平方向の色差は線形補間されます。
      〃         YV12の場合             : まず入力クリップにConvertToYV16()を掛けます。
                                          後の挙動はYV16の場合と同じです。
      〃         YV411の場合            : クリップを読み込みません。
      〃         YUY2/RGB24/RGB32の場合 : そのままYUY2/RGB24として読み込みます。

   16bitYUV->YC48の変換式は次のようになっています。
      YC48_Y  = ((16bit_y * 4788) / 256) - 299
      YC48_Cb = ((16bit_u - 32768) * 4683) / 256
      YC48_Cr = ((16bit_v - 32768) * 4683) / 256
   いずれも符号付き32bit整数の精度で計算した後、符号付き16bit整数にキャストしています。

   なお、avsreader.iniを削除したり、他の場所に移動させた場合、元の場所に再度作られます。


ソースコード:
https://github.com/chikuzen/AvsReader/
