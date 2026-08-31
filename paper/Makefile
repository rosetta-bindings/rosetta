PAPER = rosetta

all: $(PAPER).pdf

$(PAPER).pdf: $(PAPER).tex $(PAPER).bib
	pdflatex -interaction=nonstopmode $(PAPER).tex
	bibtex $(PAPER)
	pdflatex -interaction=nonstopmode $(PAPER).tex
	pdflatex -interaction=nonstopmode $(PAPER).tex

# List everything still missing a number or a citation.
todo:
	@grep -n '\\todo{\|\\gap{\|INCOMPLETE' $(PAPER).tex $(PAPER).bib

clean:
	rm -f $(PAPER).aux $(PAPER).bbl $(PAPER).blg $(PAPER).log $(PAPER).out $(PAPER).pdf

.PHONY: all todo clean
