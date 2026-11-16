function t(re, s) { try { return re.test(s); } catch(e) { return "ERR:"+e.message; } }
print("matchAll: " + JSON.stringify([..."a\u0301b".matchAll(/./gu)].map(x=>x[0])));
var props = ["L","Lu","Ll","Lt","Lm","Lo","M","Mn","Mc","Me","N","Nd","Nl","No","P","Pc","Pd","Ps","Pe","Pi","Pf","Po","S","Sm","Sc","Sk","So","Z","Zl","Zp","Zs","C","Cc","Cf","Cs","Co","Cn","White_Space","Alphabetic","Any","ASCII","Assigned","Emoji","Emoji_Presentation","Extended_Pictographic"];
for (var i=0;i<props.length;i++) {
  try { new RegExp("\\p{"+props[i]+"}","u"); print(props[i]+": ok"); } catch(e){ print(props[i]+": FAIL "+e.message); }
}
var scripts = ["Latin","Greek","Han","Cyrillic","Hebrew","Arabic","Hindi","Tamil","Telugu","Thai","Japanese","Korean","Common","Inherited"];
for (var i=0;i<scripts.length;i++) {
  try { new RegExp("\\p{Script="+scripts[i]+"}","u"); print("Script="+scripts[i]+": ok"); } catch(e){ print("Script="+scripts[i]+": FAIL "+e.message); }
}
var gc = ["Letter","Lowercase_Letter","Uppercase_Letter","Nonspacing_Mark","Spacing_Mark","Decimal_Number","Connector_Punctuation","Dash_Punctuation","Open_Punctuation","Close_Punctuation","Initial_Punctuation","Final_Punctuation","Other_Punctuation","Math_Symbol","Currency_Symbol","Modifier_Symbol","Other_Symbol","Space_Separator","Line_Separator","Paragraph_Separator","Control","Format","Surrogate","Private_Use","Unassigned"];
for (var i=0;i<gc.length;i++) {
  try { new RegExp("\\p{General_Category="+gc[i]+"}","u"); print("GC="+gc[i]+": ok"); } catch(e){ print("GC="+gc[i]+": FAIL "+e.message); }
}
var binary = ["Grapheme_Base","Grapheme_Extend","Grapheme_Link","Regional_Indicator","Emoji_Modifier","Emoji_Modifier_Base"];
for (var i=0;i<binary.length;i++) {
  try { new RegExp("\\p{"+binary[i]+"}","u"); print(binary[i]+": ok"); } catch(e){ print(binary[i]+": FAIL "+e.message); }
}
