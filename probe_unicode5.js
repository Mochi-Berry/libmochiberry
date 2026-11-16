var props = ["Grapheme_Base","Grapheme_Extend","Grapheme_Link","Regional_Indicator","Emoji_Modifier","Emoji_Modifier_Base","Extended_Pictographic","Default_Ignorable_Code_Point","ID_Continue","ID_Start","White_Space","Emoji","Emoji_Component","Emoji_Presentation","Math","Ideographic","Hex_Digit","Join_Control","Variation_Selector","Prepended_Concatenation_Mark","Alphabetic","Assigned","Any","ASCII"];
for (var i = 0; i < props.length; i++) {
    var ok = "NO";
    try { new RegExp("\\p{" + props[i] + "}", "u"); ok = "YES"; } catch (e) { ok = "NO"; }
    print(props[i] + " = " + ok);
}
print("---");
function t(name, re, s) {
    var r;
    try { r = re.test(s); } catch (e) { r = "ERR"; }
    print(name + " -> " + r);
}
t("ExtPict 1F600", new RegExp("\\p{Extended_Pictographic}", "u"), String.fromCodePoint(0x1F600));
t("ExtPict 1F44B", new RegExp("\\p{Extended_Pictographic}", "u"), String.fromCodePoint(0x1F44B));
t("ExtPict 1F468", new RegExp("\\p{Extended_Pictographic}", "u"), String.fromCodePoint(0x1F468));
t("ExtPict 1F470", new RegExp("\\p{Extended_Pictographic}", "u"), String.fromCodePoint(0x1F470));
t("ExtPict 1F3FB", new RegExp("\\p{Extended_Pictographic}", "u"), String.fromCodePoint(0x1F3FB));
t("RI 1F1E6", new RegExp("\\p{Regional_Indicator}", "u"), String.fromCodePoint(0x1F1E6));
t("GExt 0301", new RegExp("\\p{Grapheme_Extend}", "u"), "\u0301");
t("GExt 1F3FB", new RegExp("\\p{Grapheme_Extend}", "u"), String.fromCodePoint(0x1F3FB));
t("GExt FE0F", new RegExp("\\p{Grapheme_Extend}", "u"), "\uFE0F");
t("GExt 200C", new RegExp("\\p{Grapheme_Extend}", "u"), "\u200C");
t("Mn 0301", new RegExp("\\p{Mn}", "u"), "\u0301");
t("Mn 0E34", new RegExp("\\p{Mn}", "u"), "\u0E34");
t("Mn 0E49", new RegExp("\\p{Mn}", "u"), "\u0E49");
t("Mc 0903", new RegExp("\\p{Mc}", "u"), "\u0903");
t("Cc 0001", new RegExp("\\p{Cc}", "u"), "\u0001");
t("Cf 00AD", new RegExp("\\p{Cf}", "u"), "\u00AD");
t("Script=Hangul 1102", new RegExp("\\p{Script=Hangul}", "u"), "\u1102");
t("Script=Hangul AC00", new RegExp("\\p{Script=Hangul}", "u"), "\uAC00");
t("Prepend 0600", new RegExp("\\p{Prepended_Concatenation_Mark}", "u"), "\u0600");
