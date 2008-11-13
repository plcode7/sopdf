#pragma once

// soPdf file
typedef struct _soPdfFile
{
    char*   fileName;
    char*   title;
    char*   author;
    char*   category;
    char*   password;

    // pdf document state
    pdf_xref        *xref;
    pdf_pagetree    *pageTree;
    pdf_page        *page;
    pdf_outline     *outline;

    // for editing
    fz_obj          *pagelist;
    fz_obj          *editobjs;

} soPdfFile, *soPdfFilePtr;


soPdfFile* 
initSoPdfFile(
    soPdfFile* pdfFile, 
    char* pdfFileName,
    char* pdfFileTitle = NULL,
    char* pdfFileAuthor = NULL,
    char* pdfFileCategory = NULL);

int 
processPdfFile(
    soPdfFile* inFile, 
    soPdfFile* outFile);