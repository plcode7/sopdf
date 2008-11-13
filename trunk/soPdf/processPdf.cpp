#include "StdAfx.h"
#include "soPdf.h"
#include "processPdf.h"

//
// Pdf variables
//

soPdfFile*
initSoPdfFile(
    soPdfFile* pdfFile, 
    char* pdfFileName,
    char* pdfTitle,
    char* pdfAuthor,
    char* pdfCategory
    )
{
    memset(pdfFile, 0, sizeof(soPdfFile));

    pdfFile->fileName = pdfFileName;
    pdfFile->title = pdfTitle;
    pdfFile->author = pdfAuthor;
    pdfFile->category = pdfCategory;
    pdfFile->password = "";

    return pdfFile;
}


int
openPdfFile(
    soPdfFile* pdfFile
    )
{
    fz_error    *error;
    fz_obj      *obj;

    //
    // open pdf and load xref table
    error = pdf_newxref(&pdfFile->xref);
    if (error)
        return soPdfError(error);

    error = pdf_loadxref(pdfFile->xref, pdfFile->fileName);
    if (error)
        return soPdfError(error);

    //
    // Handle encrypted file
    error = pdf_decryptxref(pdfFile->xref);
    if (error)
        return soPdfError(error);

    if (pdfFile->xref->crypt)
    {
        int ret = pdf_setpassword(pdfFile->xref->crypt, 
            pdfFile->password);
        if (! ret)
            return soPdfError(fz_throw("invalid password"));
    }

    //
    // load the page tree and other objects
    error = pdf_loadpagetree(&pdfFile->pageTree, pdfFile->xref);
    if (error)
        return soPdfError(error);

    //
    // load meta information
    obj = fz_dictgets(pdfFile->xref->trailer, "Root");
    if (! obj)
        return soPdfError(fz_throw("mising root object"));

    error = pdf_loadindirect(&pdfFile->xref->root, pdfFile->xref, obj);
    if (error)
        return soPdfError(error);


    obj = fz_dictgets(pdfFile->xref->trailer, "Info");
    if (obj)
    {
        error = pdf_loadindirect(&pdfFile->xref->info, pdfFile->xref, obj);
        if (error)
            return soPdfError(error);
    }

    error = pdf_loadnametrees(pdfFile->xref);
    if (error)
        return soPdfError(error);

    error = pdf_loadoutline(&pdfFile->outline, pdfFile->xref);
    if (error)
        return soPdfError(error);

    return 0;
}

int
closePdfFile(
    soPdfFile* pdfFile
    )
{
    assert(pdfFile != NULL);

    if (pdfFile->pageTree)
    {
        pdf_droppagetree(pdfFile->pageTree);
        pdfFile->pageTree = NULL;
    }

    if (pdfFile->xref)
    {
        if (pdfFile->xref->store)
        {
            pdf_dropstore(pdfFile->xref->store);
            pdfFile->xref->store = NULL;
        }

        pdf_closexref(pdfFile->xref);
    }

    return 0;
}

int
newPdfFile(
    soPdfFile* pdfFile
    )
{
    fz_error    *error;

    assert(pdfFile != NULL);

    error = pdf_newxref(&pdfFile->xref);
    if (error)
        return soPdfError(error);

    error = pdf_initxref(pdfFile->xref);
    if (error)
        return soPdfError(error);

    error = fz_newarray(&pdfFile->pagelist, 100);
    if (error)
        return soPdfError(error);

    error = fz_newarray(&pdfFile->editobjs, 100);
    if (error)
        return soPdfError(error);

    return 0;
}


int
copyPdfFile(
    soPdfFile* inFile,
    soPdfFile* outFile
    )
{
    fz_error    *error;
    fz_obj      *results;
    int         pageNo, ctr;
    int         rootNum, rootGen;
    int         listNum, listGen;

    assert(inFile != NULL);
    assert(outFile != NULL);

    for (pageNo = 0; pageNo < pdf_getpagecount(inFile->pageTree); pageNo++)
    {
        // Get the page object from the source
        int     sNum, sGen;
        fz_obj  *pageObj2, *pageRef2;
        fz_obj  *pageRef = inFile->pageTree->pref[pageNo];
        fz_obj  *pageObj = pdf_getpageobject(inFile->pageTree, pageNo);

        // delete the parent dictionary entry
        // Do we need to delete any other dictionary entry 
        // like annot, tabs, metadat, etc
        fz_dictdels(pageObj, "Parent");

        pdf_updateobject(inFile->xref, fz_tonum(pageRef), fz_togen(pageRef), pageObj);

        // push it into destination
        error = fz_arraypush(outFile->editobjs, pageRef);
        if (error)
            return soPdfError(error);

        // copy the source dictionary entry
        error = pdf_allocobject(inFile->xref, &sNum, &sGen);
        if (error)
            return soPdfError(error);

        error = fz_copydict(&pageObj2, pageObj);
        if (error)
            return soPdfError(error);

        pdf_updateobject(inFile->xref, sNum, sGen, pageObj2);

        fz_dropobj(pageObj2);

        error = fz_newindirect(&pageRef2, sNum, sGen);
        if (error)
            return soPdfError(error);

        error = fz_arraypush(outFile->editobjs, pageRef2);
        if (error)
            return soPdfError(error);

    }

    // flush the objects into destination
    error = pdf_transplant(outFile->xref, inFile->xref, &results, outFile->editobjs);
    if (error)
        return soPdfError(error);

    for (ctr = 0; ctr < fz_arraylen(results); ctr++)
    {
        error = fz_arraypush(outFile->pagelist, fz_arrayget(results, ctr));
        if (error)
            return soPdfError(error);
    }

    fz_dropobj(results);


    // flush page tree

    // Create page tree and add back-links
    {
        fz_obj  *pageObj;
        fz_obj  *pageRef;

        error = pdf_allocobject(outFile->xref, &listNum, &listGen);
        if (error)
            return soPdfError(error);

        error = fz_packobj(&pageObj, "<</Type/Pages/Count %i/Kids %o>>",
            fz_arraylen(outFile->pagelist), outFile->pagelist);
        if (error)
            return soPdfError(error);

        pdf_updateobject(outFile->xref, listNum, listGen, pageObj);

        fz_dropobj(pageObj);

        error = fz_newindirect(&pageRef, listNum, listGen);
        if (error)
            return soPdfError(error);

        for (ctr = 0; ctr < fz_arraylen(outFile->pagelist); ctr++)
        {
            int num = fz_tonum(fz_arrayget(outFile->pagelist, ctr));
            int gen = fz_togen(fz_arrayget(outFile->pagelist, ctr));

            error = pdf_loadobject(&pageObj, outFile->xref, num, gen);
            if (error)
                return soPdfError(error);

            error = fz_dictputs(pageObj, "Parent", pageRef);
            if (error)
                return soPdfError(error);

            pdf_updateobject(outFile->xref, num, gen, pageObj);

            fz_dropobj(pageObj);
        }
    }

    // Create catalog
    {
        fz_obj  *catObj;

        error = pdf_allocobject(outFile->xref, &rootNum, &rootGen);
        if (error)
            return soPdfError(error);

        error = fz_packobj(&catObj, "<</Type/Catalog/Pages %r>>", listNum, listGen);
        if (error)
            return soPdfError(error);

        pdf_updateobject(outFile->xref, rootNum, rootGen, catObj);

        fz_dropobj(catObj);
    }

    // Create trailer
    error = fz_packobj(&outFile->xref->trailer, "<</Root %r>>", rootNum, rootGen);
    if (error)
        return soPdfError(error);


    // Save the xref
    error = pdf_savexref(outFile->xref, outFile->fileName, NULL);
    if (error)
        return soPdfError(error);

    return 0;
}


int 
processPdfFile(
    soPdfFile* inFile,
    soPdfFile* outFile
    )
{
    int retCode = 0;

    assert(inFile != NULL);
    assert(outFile != NULL);

    // Open the input file
    retCode = openPdfFile(inFile);
    if (retCode != 0)
        goto Cleanup;

    // Create an output file
    retCode = newPdfFile(outFile);
    if (retCode != 0)
        goto Cleanup;
    
    // Copy from source to destination
    retCode = copyPdfFile(inFile, outFile);
    if (retCode != 0)
        goto Cleanup;

Cleanup:

    closePdfFile(inFile);
    closePdfFile(outFile);

    return retCode;
}