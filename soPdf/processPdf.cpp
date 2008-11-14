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


void
indent(int level)
{
    while(level--) putchar(' ');
}

void 
bbdump(fz_node *node, int level)
{
	fz_node *child;
    char*   kind;
    
    if (! node) return;

    switch(node->kind) 
    {
	case FZ_NOVER:      kind = "over"; break;
	case FZ_NMASK:      kind = "mask"; break;
	case FZ_NBLEND:     kind = "blend"; break;
	case FZ_NTRANSFORM: kind = "transform"; break;
	case FZ_NCOLOR:     kind = "color"; break;
	case FZ_NPATH:      kind = "path"; break;
	case FZ_NTEXT:      kind = "text"; break;
	case FZ_NIMAGE:     kind = "image"; break;
	case FZ_NSHADE:     kind = "shade"; break;
	case FZ_NLINK:      kind = "link"; break;
    default:            kind = "UNK"; break;
    }

    indent(level);
    printf("<%s : bbox = %.2f,%.2f - %.2f,%.2f>\n", 
        kind, node->bbox.x0, node->bbox.y0, 
        node->bbox.x1, node->bbox.y1);

    for (child = node->first; child; child = child->next)
		bbdump(child, level + 1);
}

fz_error*
setPageMediaBox(
    pdf_xref*   pdfXRef,
    fz_obj*     pageObj,
    fz_rect     mediaBox
    )
{
    fz_error    *error;
    fz_obj      *objMedia;
    fz_irect    mRect;
    fz_obj      *objInt;

    // Delete the CropBox. This is done because we are reducing
    // the size of the media box and CropBox is of no use to us
    fz_dictdels(pageObj, "CropBox");

    // Get the media box
    objMedia = fz_dictgets(pageObj, "MediaBox");
    if (objMedia == NULL)
    {
        // This entry does not have media box. Create a new
        // media box entry later
        return fz_throw("no MediaBox entry");
    }

    error = pdf_resolve(&objMedia, pdfXRef);
    if (error)
        return fz_rethrow(error, "cannot resolve page bounds");

    if (! fz_isarray(objMedia))
        return fz_throw("cannot find page bounds");

    // We have the MediaBox array here
    mRect = fz_roundrect(mediaBox);
    objInt = fz_arrayget(objMedia, 0); objInt->u.i = mRect.x0;
    objInt = fz_arrayget(objMedia, 1); objInt->u.i = mRect.y0;
    objInt = fz_arrayget(objMedia, 2); objInt->u.i = mRect.x1;
    objInt = fz_arrayget(objMedia, 3); objInt->u.i = mRect.y1;

    return NULL;
}

fz_error*
setPageRotate(
    fz_obj*     pageObj,
    int         rotate
    )
{
    fz_obj      *objRotate;

    // Get the media box
    objRotate = fz_dictgets(pageObj, "Rotate");
    if (objRotate == NULL)
    {
        // This entry does not have media box. Create a new
        // media box entry later
        return fz_throw("no Rotate entry");
    }

    objRotate->u.i = rotate;

    return NULL;
}


bool
isInsideRect(
    fz_rect maxRect,
    fz_rect checkRect
    )
{
    if (fz_isinfiniterect(checkRect) || fz_isemptyrect(checkRect))
        return true;

    return ((maxRect.x0 <= checkRect.x0) && 
            (maxRect.y0 <= checkRect.y0) &&
            (maxRect.x1 >= checkRect.x1) &&
            (maxRect.y1 >= checkRect.y1));
}

fz_rect
getContainingRect(
    fz_node *node, 
    fz_rect maxRect
    )
{
    fz_rect rect = fz_emptyrect;

    if (node)
    {
        switch(node->kind)
        {
        case FZ_NTEXT:
        case FZ_NIMAGE:
        case FZ_NPATH:
            if (isInsideRect(maxRect, node->bbox))
                rect = fz_mergerects(rect, node->bbox);
            break;

        default:
            break;
        }

        // Recurse
        for (fz_node *child = node->first; child; child = child->next)
            rect = fz_mergerects(rect, getContainingRect(child, maxRect));
    }

    return rect;
}

#define SPLIT_POINTS 1000
struct splitPoints
{
    float y0[SPLIT_POINTS];
    float y1[SPLIT_POINTS];
    int count;
};

int splitCmp(const void* e1, const void* e2)
{
    return (*(float*)e1 < *(float*)e2) ? -1 : ((*(float*)e1 > *(float*)e2) ? 1 : 0);
}

fz_error* insertYCoord(splitPoints *sp, float y0, float y1)
{
    int ctr;

    if (sp->count >= SPLIT_POINTS)
    {
        return fz_throw("not enough memory");
    }

    if (sp->count > 0)
    {
        float *fp = (float*)bsearch(&y0, sp->y0, sp->count, sizeof(float), splitCmp);
        if (fp != NULL)
        {
            int offset = (fp - sp->y0) / sizeof(float);
            if (sp->y1[offset] < y1) sp->y1[offset] = y1;
            return NULL;
        }
    }

    // Insert sorted
    for (ctr = sp->count; ctr > 0; ctr--)
    {
        if (sp->y0[ctr - 1] > y0) 
        {
            sp->y0[ctr] = sp->y0[ctr - 1];
            sp->y1[ctr] = sp->y1[ctr - 1];
        }
        else
            break;
    }

    sp->y0[ctr] = y0;
    sp->y1[ctr] = y1;
    sp->count++;

    return NULL;
} 


void
getSplitPoints(
    fz_node *node,
    splitPoints *sp
    )
{
    if (node)
    {
        switch(node->kind)
        {
        case FZ_NTEXT:
        case FZ_NIMAGE:
        case FZ_NPATH:
            insertYCoord(sp, node->bbox.y0, node->bbox.y1);
            break;

        default:
            break;
        }

        // Recurse
        for (fz_node *child = node->first; child; child = child->next)
            getSplitPoints(child, sp);
    }
}

void
processSplitPoints(
    splitPoints *sp
    )
{
    splitPoints lsp;
    int prevCount;

    do {

        lsp.count = 1;
        lsp.y0[0] = sp->y0[0];
        lsp.y1[0] = sp->y1[0];

        for (int ctr = 1; ctr < sp->count; ctr++)
        {
            if (lsp.y1[lsp.count - 1] < sp->y0[ctr])
                insertYCoord(&lsp, sp->y0[ctr], sp->y1[ctr]);
            else
            {
                if (sp->y0[ctr] < lsp.y0[lsp.count - 1])    lsp.y0[lsp.count - 1] = sp->y0[ctr];
                if (sp->y1[ctr] > lsp.y1[lsp.count - 1])    lsp.y1[lsp.count - 1] = sp->y1[ctr];
            }
        }

        // copy to dest
        prevCount = sp->count;
        *sp = lsp;

    } while (prevCount > lsp.count);
}

fz_error*
processPage(
    soPdfFile* inFile,
    int pageNo,
    fz_rect *bbRect,
    int rectCount
    )
{
    fz_error    *error;
    fz_obj      *pageRef;
    pdf_page    *pdfPage;
    fz_rect     contentBox;
    fz_rect     mediaBox;
    splitPoints sp;

    // Initialize 
    sp.count = 0;
    for (int ctr = 0; ctr < rectCount; ctr++)
        bbRect[ctr] = fz_emptyrect;

    // Get the page reference and load it
    pageRef = pdf_getpageobject(inFile->pageTree, pageNo);
    error = pdf_loadpage(&pdfPage, inFile->xref, pageRef);
    if (error)
        return error;


    // Get the bounding box for the page
    mediaBox = pdfPage->mediabox;
    //width = mediaBox.x1 - mediaBox.x0;
    //height = mediaBox.y1 - mediaBox.y0;

    // calculate the bounding box for all the elements in the page
    contentBox = fz_boundnode(pdfPage->tree->root, fz_identity());

    // If there is nothing on the page we return nothing.
    // should we return and empty page instead ???
    if (fz_isemptyrect(contentBox))
    {
        pdf_droppage(pdfPage);
        return error;
    }


    printf("-->Page %d\n", pageNo);
#ifdef _blahblah
    bbdump(pdfPage->tree->root, 1);
#endif

    //// Get all the points where the page can be split
    //getSplitPoints(pdfPage->tree->root, &sp);
    //processSplitPoints(&sp);

    // Get the first split
    float contentHeight = contentBox.y1 - contentBox.y0;
    bbRect[0] = contentBox;
    bbRect[0].y0 = bbRect[0].y0 + contentHeight / 2;
    bbRect[0] = getContainingRect(pdfPage->tree->root, bbRect[0]);

    if (fz_isemptyrect(bbRect[0]))
    {
        bbRect[0] = contentBox;
        bbRect[0].y0 = bbRect[0].y0 + contentHeight / 2;
    }

    // Check if we need second split
    float firstSplitHeight = bbRect[0].y1 - bbRect[0].y0;
    if (firstSplitHeight / contentHeight * 100 < 40)
    {

    }
//    bbRect[0] = contentBox;

    // done with the page
    pdf_droppage(pdfPage);

    return error;
}

int
copyPdfFile(
    soPdfFile* inFile,
    soPdfFile* outFile
    )
{
    fz_error    *error;
    int         pageTreeNum, pageTreeGen;

    assert(inFile != NULL);
    assert(outFile != NULL);

    //
    // Process every page in the source file
    //
    {
        for (int pageNo = 0; pageNo < pdf_getpagecount(inFile->pageTree); pageNo++)
        {
            // Get the page object from the source
            int     sNum, sGen;
            fz_obj  *pageObj2, *pageRef2;
            fz_obj  *pageRef = inFile->pageTree->pref[pageNo];
            fz_obj  *pageObj = pdf_getpageobject(inFile->pageTree, pageNo);

            //
            // Process the page. Each page can be split into up-to 3 pages
            //
            fz_rect    bbRect[3];
            error = processPage(inFile, pageNo, bbRect, 3);
            if (error)
                return soPdfError(error);

            //
            // Set the media box
            //
            setPageMediaBox(inFile->xref, pageObj, bbRect[0]);

            // delete the parent dictionary entry
            // Do we need to delete any other dictionary entry 
            // like annot, tabs, metadata, etc
            fz_dictdels(pageObj, "Parent");

            pdf_updateobject(inFile->xref, fz_tonum(pageRef), fz_togen(pageRef), pageObj);

            // push it into destination
            error = fz_arraypush(outFile->editobjs, pageRef);
            if (error)
                return soPdfError(error);

            ////
            //// copy the source page dictionary entry. The way this is done is basically
            //// by making a copy of the page dict object in the source file, and adding
            //// the copy in the source file. Then the copied page dict object is 
            //// referenced and added to the destination file.
            ////
            //// This convoluted procedure is done because the copy is done by pdf_transplant
            //// function that accepts a source and destination. What ever is referenced by
            //// destination object is deep copied
            ////

            //// allocate an object id and generation id in source file
            //error = pdf_allocobject(inFile->xref, &sNum, &sGen);
            //if (error)
            //    return soPdfError(error);

            //// make a copy of the original page dict
            //error = fz_copydict(&pageObj2, pageObj);
            //if (error)
            //    return soPdfError(error);

            //// update the source file with the duplicate page object
            //pdf_updateobject(inFile->xref, sNum, sGen, pageObj2);

            //fz_dropobj(pageObj2);

            //// create an indirect reference to the page object
            //error = fz_newindirect(&pageRef2, sNum, sGen);
            //if (error)
            //    return soPdfError(error);

            //// push the indirect reference to the destination list for copy by pdf_transplant
            //error = fz_arraypush(outFile->editobjs, pageRef2);
            //if (error)
            //    return soPdfError(error);
        }
    }

    // flush the objects into destination from source
    {
        fz_obj      *results;

        error = pdf_transplant(outFile->xref, inFile->xref, &results, outFile->editobjs);
        if (error)
            return soPdfError(error);

        for (int ctr = 0; ctr < fz_arraylen(results); ctr++)
        {
            error = fz_arraypush(outFile->pagelist, fz_arrayget(results, ctr));
            if (error)
                return soPdfError(error);
        }

        fz_dropobj(results);
    }

    // flush page tree

    // Create page tree and add back-links
    {
        fz_obj  *pageTreeObj;
        fz_obj  *pageTreeRef;

        // allocate a new object in out file for pageTree object
        error = pdf_allocobject(outFile->xref, &pageTreeNum, &pageTreeGen);
        if (error)
            return soPdfError(error);

        // Create a page tree object
        error = fz_packobj(&pageTreeObj, "<</Type/Pages/Count %i/Kids %o>>",
            fz_arraylen(outFile->pagelist), outFile->pagelist);
        if (error)
            return soPdfError(error);

        // Update the xref entry with the pageTree object
        pdf_updateobject(outFile->xref, pageTreeNum, pageTreeGen, pageTreeObj);

        fz_dropobj(pageTreeObj);

        // Create a reference to the pageTree object
        error = fz_newindirect(&pageTreeRef, pageTreeNum, pageTreeGen);
        if (error)
            return soPdfError(error);

        //
        // For every page in the output file, update the parent entry
        //
        for (int ctr = 0; ctr < fz_arraylen(outFile->pagelist); ctr++)
        {
            fz_obj  *pageObj;

            int num = fz_tonum(fz_arrayget(outFile->pagelist, ctr));
            int gen = fz_togen(fz_arrayget(outFile->pagelist, ctr));

            // Get the page object from xreft
            error = pdf_loadobject(&pageObj, outFile->xref, num, gen);
            if (error)
                return soPdfError(error);

            // Update the parent entry in the page dictionary
            error = fz_dictputs(pageObj, "Parent", pageTreeRef);
            if (error)
                return soPdfError(error);

            // Update the entry with the updated page object
            pdf_updateobject(outFile->xref, num, gen, pageObj);

            fz_dropobj(pageObj);
        }
    }

    // Create catalog and root entries
    {
        fz_obj  *catObj;
        int     rootNum, rootGen;

        error = pdf_allocobject(outFile->xref, &rootNum, &rootGen);
        if (error)
            return soPdfError(error);

        error = fz_packobj(&catObj, "<</Type/Catalog/Pages %r>>", pageTreeNum, pageTreeGen);
        if (error)
            return soPdfError(error);

        pdf_updateobject(outFile->xref, rootNum, rootGen, catObj);

        fz_dropobj(catObj);

        // Create trailer
        error = fz_packobj(&outFile->xref->trailer, "<</Root %r>>", rootNum, rootGen);
        if (error)
            return soPdfError(error);

    }


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