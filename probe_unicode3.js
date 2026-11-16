var binary = ["Grapheme_Base","Grapheme_Extend","Grapheme_Link","Regional_Indicator","Emoji_Modifier","Emoji_Modifier_Base","Extended_Pictographic","Default_Ignorable_Code_Point","IDS_Binary_Operator","ID_Continue","ID_Start","XID_Continue","XID_Start","Ideographic","Hex_Digit","Join_Control","Noncharacter_Code_Point","Pattern_Syntax","Pattern_White_Space","Prepended_Concatenation_Mark","Quotation_Mark","Radical","Sentence_Terminal","Soft_Dotted","Terminal_Punctuation","Unified_Ideograph","Variation_Selector","White_Space","Emoji","Emoji_Component","Emoji_Modifier","Emoji_Presentation","Math"];
for (var i=0;i<binary.length;i++) {
  try { new RegExp("\\p{"+binary[i]+"}","u"); print(binary[i]+": ok"); } catch(e){ print(binary[i]+": FAIL "+e.message); }
}
var gcb = ["CR","LF","Control","Extend","ZWJ","Regional_Indicator","Prepend","SpacingMark","L","V","T","LV","LVT"];
for (var i=0;i<gcb.length;i++) {
  try { new RegExp("\\p{Grapheme_Cluster_Break="+gcb[i]+"}","u"); print("GCB="+gcb[i]+": ok"); } catch(e){ print("GCB="+gcb[i]+": FAIL "+e.message); }
}
print("--- test \\p{gcb=Extend} on combining acute: " + /\p{Grapheme_Cluster_Break=Extend}/u.test("\u0301"));
print("--- test Regional_Indicator on U+1F1E6: " + /\p{Regional_Indicator}/u.test("\u{1F1E6}"));
print("--- test Hangul L U+1102: " + /\p{Grapheme_Cluster_Break=L}/u.test("\u1102"));
print("--- ExtPict on emoji: " + /\p{Extended_Pictographic}/u.test("\u{1F600}"));
print("--- ZWJ: " + /\p{Grapheme_Cluster_Break=ZWJ}/u.test("\u200D"));
print("--- CR: " + /\p{Grapheme_Cluster_Break=CR}/u.test("\r"));
print("--- Control: " + /\p{Grapheme_Cluster_Break=Control}/u.test("\u0001"));
print("--- Prepend: " + /\p{Grapheme_Cluster_Break=Prepend}/u.test("\u0600"));
print("--- SpacingMark: " + /\p{Grapheme_Cluster_Break=SpacingMark}/u.test("\u0903"));
