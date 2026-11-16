var props = ["L","Lu","Ll","M","Mn","Mc","Me","N","Nd","Nl","No","P","S","Z","C","Cc","Cf","Cs","Co","Cn",
"Grapheme_Extend","Grapheme_Base","Extended_Pictographic","Regional_Indicator","Emoji","Emoji_Modifier",
"Emoji_Presentation","Emoji_Component","White_Space","Alphabetic","ID_Start","ID_Continue","Join_Control",
"Default_Ignorable_Code_Point","Hex_Digit","Math","Ideographic","Variation_Selector","Prepended_Concatenation_Mark",
"Case_Ignorable","Changes_When_Casefolded","Lowercase","Uppercase","Titlecase","Katakana","Hiragana","Han",
"Script=Hangul","Script=Thai","Script=Latin","Script=Hebrew","Script=Arabic","Script=Hiragana","Script=Katakana",
"Script=Han","Script=Cyrillic","Script=Devanagari","Script=Tamil","Script=Telugu","Script=Thai","Script=Greek",
"General_Category=Letter","General_Category=Mark","General_Category=Number"];
var out = [];
for (var i = 0; i < props.length; i++) {
  var p = props[i];
  try { new RegExp("\\p{" + p + "}", "u"); out.push(p + ": OK"); }
  catch (e) { out.push(p + ": ERR " + e.message); }
}
try { out.push("gcb=Extend: OK"); new RegExp("\\p{Grapheme_Cluster_Break=Extend}", "u"); } catch(e) { out.push("gcb=Extend: ERR"); }
// functional checks
function chk(name, re, cps) {
  var r;
  try { r = new RegExp(re, "u"); } catch(e) { out.push(name + ": REERR " + e.message); return; }
  var res = [];
  for (var i = 0; i < cps.length; i++) {
    res.push(cps[i].toString(16) + "=" + (r.test(String.fromCodePoint(cps[i])) ? "Y" : "n"));
  }
  out.push(name + ": " + res.join(","));
}
chk("Extend@0301,1F3FB,200D,0903", "\\p{Grapheme_Extend}", [0x301,0x1F3FB,0x200D,0x903,0xE34,0xE49]);
chk("ExtPict@1F44B,1F468,1F470,00A9", "\\p{Extended_Pictographic}", [0x1F44B,0x1F468,0x1F470,0xA9,0x1F600,0x2764]);
chk("RI@1F1E6,1F1FF", "\\p{Regional_Indicator}", [0x1F1E6,0x1F1FF,0x41]);
chk("Mc@0903,E34", "\\p{Mc}", [0x903,0xE34,0x301,0x903]);
chk("Mn@0301,E34,E49", "\\p{Mn}", [0x301,0xE34,0xE49,0x903]);
chk("Cf@200D,200C,00AD", "\\p{Cf}", [0x200D,0x200C,0xAD,0x903]);
chk("Cs@D800,DC00", "\\p{Cs}", [0xD800,0xDC00,0x41]);
chk("L@1102,1162,11A9,10330,1F44B", "\\p{L}", [0x1102,0x1162,0x11A9,0x10330,0x1F44B,0xE0B]);
chk("N@0031,0660", "\\p{N}", [0x31,0x660,0x41]);
var f = new Intl.Segmenter ? "HAS" : "NO";
out.push("Intl.Segmenter=" + f);
out.push("normalize NFC=" + ("a\u0301".normalize("NFC") === "\u00e1"));
out.push("length of '𐂰'=" + "\u{10330}".length);
print(out.join("\n"));
