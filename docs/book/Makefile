BOOK = rosetta-book

# Three passes: the first writes the .aux/.toc, the second resolves the
# cross-references it recorded, the third settles the page numbers those
# references may have shifted.
all: $(BOOK).pdf

$(BOOK).pdf: $(BOOK).tex $(wildcard ch/*.tex)
	pdflatex -interaction=nonstopmode $(BOOK).tex
	pdflatex -interaction=nonstopmode $(BOOK).tex
	pdflatex -interaction=nonstopmode $(BOOK).tex

# Fail loudly instead of writing a PDF with '??' in it, or with a line
# running off the page. Both should stay at zero.
check: $(BOOK).pdf
	@! grep -q 'Reference.*undefined' $(BOOK).log || \
	  { grep -n 'Reference.*undefined' $(BOOK).log; exit 1; }
	@! grep -q 'Citation.*undefined' $(BOOK).log || \
	  { grep -n 'Citation.*undefined' $(BOOK).log; exit 1; }
	@! grep -q 'Overfull' $(BOOK).log || \
	  { grep -n -A2 'Overfull' $(BOOK).log; exit 1; }
	@echo "no undefined references, no overfull boxes"

# One pass, for a quick look while writing. Cross-references will be stale.
quick:
	pdflatex -interaction=nonstopmode $(BOOK).tex

open: $(BOOK).pdf
	open $(BOOK).pdf

clean:
	rm -f $(BOOK).aux $(BOOK).log $(BOOK).out $(BOOK).toc $(BOOK).lof $(BOOK).lot

distclean: clean
	rm -f $(BOOK).pdf

.PHONY: all check quick open clean distclean
