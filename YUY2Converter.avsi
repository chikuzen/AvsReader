# text encoding : CP932 (Shift_JIS)

# yuy2converterサンプル:

# requirement:
# このファイルとTIVTC.dll( http://web.missouri.edu/~kes25c/ )を、
# AviSynthのオートローディングフォルダに入れておくこと

function ConditionalConvertToYUY2(clip c)
{
    return ConditionalFilter(c,
\                            c.ConvertToYUY2(interlaced=true), 
\                            c.ConvertToYUY2(interlaced=false),
\                            "IsCombedTIVTC", "=", "true")
}
