// soPdf.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "soPdf.h"
#include "processPdf.h"

//
// Definitions
//
char*   p_szInFile;
char*   p_szOutFile;
char*   p_szTitle;
char*   p_szAuthor;
char*   p_szPublisher;
char*   p_szCategory;
char*   p_noPages;

bool    p_cropSides;
double  p_overlap;
EScale  p_scale;

// Pdf files
soPdfFile   inPdfFile;
soPdfFile   outPdfFile;

bool
ParseCommandLine()
{
    initSoPdfFile(&inPdfFile, "d:\\temp\\forexsur.pdf");
    initSoPdfFile(&outPdfFile, "d:\\temp\\forexsurout.pdf");

    p_szTitle = "test";
    p_szAuthor = "Unknown";
    p_szPublisher = "Unknown";
    p_szCategory = "Unknown";
    p_noPages = "0";
    
    p_cropSides = true;
    p_overlap = 2;
    p_scale = FitTo2xWidth;

    return true;
}


int
soPdfError(
    fz_error *error
    )
{
    printf(" Error: %s\n", error->msg);
    printf("  Func: %s\n", error->func);
    printf("  Line: %d\n", error->line);
    printf("  File: %s\n", error->file);
    if (error->cause)
    {
        printf(" Cause: -->");
        soPdfError(error->cause);
    }

    //fz_droperror(error);
    return 1;
}


int 
_tmain(int argc, _TCHAR* argv[])
{
    //
    // Parse the command line parameters
    //
    if (! ParseCommandLine())
        return 1;

    return processPdfFile(&inPdfFile, &outPdfFile);
}

