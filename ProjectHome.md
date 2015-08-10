I really liked the pdflrf tool from the "Yet another PDF to LRF converter" thread, but it has been taken down by the moderator for violation of GPL and has been down for quite some time because it seems like the author is not interested in providing the source for his tool. But there are some issues with the pdflrf tool.

  * pdflrf renderes the pdf into image and then creates the lrf file. This makes the 4mb pdf file grow into more than 40mb file.
  * No text information is preserved because of the image conversion
  * Very slow
  * No source for the tool <-- biggest disadvantage


So I decided to write a tool for myself. soPdf is a pdf formatter for sony reader. It is based on sumatrapdf's version of mupdf and fitz.

The advantages of soPdf over pdflrf

  * Pdf to Pdf conversion
  * Text and other contents of pdf are preserved
  * Size of the output file is very close to size of input file and in some cases smaller than input file.
  * Super fast conversion compared to pdflrf.
  * Source available to make further changes !!!!!! <-- biggest advantage

The disadvantages over pdflrf

  * Cannot yet convert the comic book. (Can be added since source is available)
  * soPdf is in alpha stage. (ver 0.1)
  * ???


The conversion algorithm is as follows

  * If user specified Fit2xWidth or Fit2xHeight then simply make two copies of  pdf page from source into destination pdf file.
  * Render the page and get the actual boundary box that encompasses all of the content in the page. This step removes all the white space border of the page.
  * Try to split the file first by iterating all the elements that can fit in half a page and if that does not work then split the file half way with 2% overlap (this can be changed).
  * If FitWidth or Fit2xWidth is specified then rotate the page by -90 deg.