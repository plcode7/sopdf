// soPdf.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "soPdf.h"
#include "processPdf.h"

//
// Definitions
//
bool    p_proceedWithErrors = false;
bool    p_reverseLandscape = false;
bool    p_cropWhiteSpace = true;
double  p_overlap = 2;
EMode   p_mode = Fit2xWidth;

// Pdf files
soPdfFile   inPdfFile;
soPdfFile   outPdfFile;


static int  opterr = 1;     /* if error message should be printed */
static int  optind = 1;     /* index into parent argv vector */
static int  optopt;			/* character checked for validity */
static char *optarg;		/* argument associated with option */

#define	BADCH	            (int)'?'
#define	EMSG	            ""
#define COPY_ARG(_x, _y)    strcpy_s(_x, sizeof(_x), _y)


int getopt(int nargc, char * const * nargv, const char *ostr)
{
	static char *place = EMSG;      /* option letter processing */
	register char *oli;             /* option letter list index */
	char *p;

	if (!*place) {				/* update scanning pointer */
		if (optind >= nargc || *(place = nargv[optind]) != '-') {
			place = EMSG;
			return(EOF);
		}
		if (place[1] && *++place == '-') {	/* found "--" */
			++optind;
			place = EMSG;
			return(EOF);
		}
	}					/* option letter okay? */
	if ((optopt = (int)*place++) == (int)':' ||
	    !(oli = (char*)strchr(ostr, optopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means EOF.
		 */
		if (optopt == (int)'-')
			return(EOF);
		if (!*place)
			++optind;
		if (opterr) {
			if (!(p = strrchr(*nargv, '/')))
				p = *nargv;
			else
				++p;
			(void)fprintf(stderr, "%s: illegal option -- %c\n",
			    p, optopt);
		}
		return(BADCH);
	}
	if (*++oli != ':') {			/* don't need argument */
		optarg = NULL;
		if (!*place)
			++optind;
	}
	else {					/* need an argument */
		if (*place)			/* no white space */
			optarg = place;
		else if (nargc <= ++optind) {	/* no arg */
			place = EMSG;
			if (!(p = strrchr(*nargv, '/')))
				p = *nargv;
			else
				++p;
			if (opterr)
				(void)fprintf(stderr,
				    "%s: option requires an argument -- %c\n",
				    p, optopt);
			return(BADCH);
		}
		else				/* white space */
			optarg = nargv[optind];
		place = EMSG;
		++optind;
	}
	return(optopt);				/* dump back option letter */
}


int
soPdfError(
    fz_error *error
    )
{
    int ctr = 0;
    fz_error *e = error;
    while (error)
    {
        printf("%*sError: %s(%d) : %s() - %s\n", 
            ctr++, "", error->file, error->line, 
            error->func, error->msg);

        error = error->cause;
    }

    fz_droperror(e);
    return 1;
}


int
soPdfUsage(void)
{
    fprintf(stderr,
        "about: soPdf\n"
        "   author: Navin Pai, soPdf ver " SO_PDF_VER "\n"
        "usage: \n"
        "   soPdf -i file_name [options]\n"
        "   -i file_name    input file name\n"
        "   -p password     password for input file\n"
        "   -o file_name    output file name\n"
        "   -w              turn off white space cropping\n"
        "                       default is on\n"
        "   -m nn           mode of operation\n"
        "                       0 = fit 2xWidth *\n"
        "                       1 = fit 2xHeight\n"
        "                       2 = fit Width\n"
        "                       3 = fit Height\n"
        "                       4 = smart fit Width\n"
        "                       5 = smart fit Height\n"
        "   -v nn           overlap percentage\n"
        "                       nn = 2 percent overlap *\n"
        "   -t title        set the file title\n"
        "   -a author       set the file author\n"
        "   -b publisher    set the publisher\n"
        "   -c category     set the category\n"
        "   -s subject      set the subject\n"
        "   -e              proceed with errors\n"
        "   -r              reverse landscape\n"
        "\n"
        "   * = default values\n");

    return 1;
}

int 
_tmain(int argc, _TCHAR* argv[])
{
    int c;

    if (argc < 2)
        return soPdfUsage();

    initSoPdfFile(&inPdfFile);
    initSoPdfFile(&outPdfFile);


    // parse the command line arguments
    while ((c = getopt(argc, argv, "i:p:o:t:a:b:c:s:ewm:v:r")) != -1)
    {
        switch(c)
        {
        case 'i':   COPY_ARG(inPdfFile.fileName, optarg);   break;
        case 'p':   COPY_ARG(inPdfFile.password, optarg);   break;
        case 'o':   COPY_ARG(outPdfFile.fileName, optarg);  break;

        case 't':   COPY_ARG(outPdfFile.title, optarg);     break;
        case 'a':   COPY_ARG(outPdfFile.author, optarg);    break;
        case 'b':   COPY_ARG(outPdfFile.publisher, optarg); break;
        case 'c':   COPY_ARG(outPdfFile.category, optarg);  break;
        case 's':   COPY_ARG(outPdfFile.subject, optarg);   break;

        case 'e':   p_proceedWithErrors = true;             break;
        case 'w':   p_cropWhiteSpace = false;               break;
        case 'm':   p_mode = (EMode)atoi(optarg);           break;
        case 'v':   p_overlap = atof(optarg);               break;
        case 'r':   p_reverseLandscape = true;              break;
        default:    return soPdfUsage();                    break;
        }
    }
    
    // Check if input file is specified at the minimum
    if (inPdfFile.fileName[0] == 0)
        return soPdfUsage();

    // If output file is not specified
    if (outPdfFile.fileName[0] == 0)
    {
        COPY_ARG(outPdfFile.fileName, inPdfFile.fileName);
        strcat_s(outPdfFile.fileName, sizeof(outPdfFile.fileName), "out.pdf");
    }

    printf("\nsoPdf ver " SO_PDF_VER "\n");
    printf("\tA program to reformat pdf file for sony reader\n");
    printf("\nInput : %s\n", inPdfFile.fileName);
    printf("Output: %s\n\n", outPdfFile.fileName);

    return processPdfFile(&inPdfFile, &outPdfFile);
}

