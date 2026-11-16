function chk(name, re) {
  try { print(name + ": " + re.test("x")); } catch(e) { print(name + ": FAIL " + e.message); }
}
var bin = ["Grapheme_Base","Grapheme_Extend","Grapheme_Link","Regional_Indicator","Emoji_Modifier","Emoji_Modifier_Base","Extended_Pictographic","Default_Ignorable_Code_Point","ID_Continue","ID_Start","Prepended_Concatenation_Mark","White_Space","Emoji","Emoji_Component","Emoji_Presentation","Math","Ideographic","Hex_Digit","Join_Control","Variation_Selector"];
for (var i=0;i<bin.length;i++) {
  try { new RegExp("\\p{"+bin[i]+"}","u"); print("OK  " + bin[i]); } catch(e){ print("NO  " + bin[i]); }
}
print("---");
try { print("ExtPict U+1F600: " + /\p{Extended_Pictographic}/u.test("\u{1F600}")); } catch(e){ print("err "+e.message); }
try { print("RI U+1F1E6: " + /\p{Regional_Indicator}/u.test("\u{1F1E6}")); } catch(e){ print("err "+e.message); }
try { print("GExt acute: " + /\p{Grapheme_Extend}/u.test("\u0301")); } catch(e){ print("err "+e.message); }
try { print("GEmojiMod: " + /\p{Grapheme_Extend}/u.test("\u{1F3FB}")); } catch(e){ print("err "+e.message); }
try { print("Mn acute: " + /\p{Mn}/u.test("\u0301")); } catch(e){ print("err "+e.message); }
try { print("Mc deva: " + /\p{Mc}/u.test("\u0903")); } catch(e){ print("err "+e.message); }
try { print("VS16: " + /\p{Grapheme_Extend}/u.test("\uFE0F")); } catch(e){ print("err "+e.message); }
try { print("ZWNJ: " + /\p{Grapheme_Extend}/u.test("\u200C")); } catch(e){ print("err "+e.message); }
try { print("Prepend: " + /\p{Prepended_Concatenation_Mark}/u.test("\u0600")); } catch(e){ print("err "+e.message); }
try { print("Cc: " + /\p{Cc}/u.test("\u0001")); } catch(e){ print("err "+e.message); }
try { print("Cf: " + /\p{Cf}/u.test("\u00AD")); } catch(e){ print("err "+e.message); }
try { print("HangulL: " + /\p{Script=Hangul}/u.test("\u1102")); } catch(e){ print("err "+e.message); }
