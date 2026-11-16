print("normalize=" + typeof String.prototype.normalize);
try { print("NFC=" + JSON.stringify("a\u0301".normalize("NFC"))); } catch(e) { print("normalize err " + e.message); }
try { print("propL=" + /\p{L}/u.test("a")); } catch(e) { print("propL err " + e.message); }
try { print("propScript=" + /\p{Script=Greek}/u.test("α")); } catch(e) { print("propScript err " + e.message); }
try { print("propGeneralCat=" + /\p{General_Category=Letter}/u.test("a")); } catch(e) { print("propGC err " + e.message); }
try { print("matchAll=" + JSON.stringify([..."a\u0301b".matchAll(/\X/g)])); } catch(e) { print("matchAll err " + e.message); }
try { print("dotAll=" + /./s.test("\n")); } catch(e) { print("dotAll err " + e.message); }
try { print("uFlag=" + /\u{1F600}/u.test("\u{1F600}")); } catch(e) { print("u err " + e.message); }
print("Intl=" + typeof Intl);
print("codepointAt=" + "𐂰".codePointAt(0));
print("fromCodePoint=" + String.fromCodePoint(0x10400));
