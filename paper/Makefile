PAPERBIB = rosetta

EN = en/rosetta-en
FR = fr/rosetta-fr

all: en fr

en: $(EN).pdf
fr: $(FR).pdf

# Both papers share ../figures and ../rosetta.bib, so each is built from inside
# its own language directory.
$(EN).pdf: $(EN).tex $(PAPERBIB).bib
	cd en && pdflatex -interaction=nonstopmode rosetta-en.tex
	cd en && bibtex rosetta-en
	cd en && pdflatex -interaction=nonstopmode rosetta-en.tex
	cd en && pdflatex -interaction=nonstopmode rosetta-en.tex

$(FR).pdf: $(FR).tex $(PAPERBIB).bib
	cd fr && pdflatex -interaction=nonstopmode rosetta-fr.tex
	cd fr && bibtex rosetta-fr
	cd fr && pdflatex -interaction=nonstopmode rosetta-fr.tex
	cd fr && pdflatex -interaction=nonstopmode rosetta-fr.tex

# List everything still missing a number or a citation.
todo:
	@grep -n '\\todo{\|\\gap{\|INCOMPLETE' $(EN).tex $(FR).tex $(PAPERBIB).bib

clean:
	rm -f $(EN).aux $(EN).bbl $(EN).blg $(EN).log $(EN).out $(EN).pdf
	rm -f $(FR).aux $(FR).bbl $(FR).blg $(FR).log $(FR).out $(FR).pdf

.PHONY: all en fr todo clean
