// Sample01.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

//
// Current document state
//
pdf_xref        *g_xref = NULL;
pdf_pagetree    *g_srcpages = NULL;
fz_renderer     *g_drawgc = NULL;
pdf_page        *g_drawpage = NULL;
pdf_outline     *g_outline = NULL;


fz_error* 
openfile(char *filename, char *password)
{
    fz_error    *error;
    fz_obj      *obj;

    //
    // Open pdf and load xref table
    //
    error = pdf_newxref(&g_xref);
    if (error)
        return error;

    error = pdf_loadxref(g_xref, filename);
    if (error)
        return error;

    //
    // Handle encrypted PDF files
    //
    error = pdf_decryptxref(g_xref);
    if (error)
        return error;

    if (g_xref->crypt)
    {
        int ret = pdf_setpassword(g_xref->crypt, password);
        if (! ret)
            return fz_throw("invalid password");
    }

    //
    // Load page tree
    //
    error = pdf_loadpagetree(&g_srcpages, g_xref);
    if (error)
        return error;

    //
    // Load meta information
    //
    obj = fz_dictgets(g_xref->trailer, "Root");
    if (! obj)
        return fz_throw("syntaxerror: missing root object");

    error = pdf_loadindirect(&g_xref->root, g_xref, obj);
    if (error)
        return error;

    obj = fz_dictgets(g_xref->trailer, "Info");
    if (obj)
    {
        error = pdf_loadindirect(&g_xref->info, g_xref, obj);
        if (error)
            return error;
    }

    error = pdf_loadnametrees(g_xref);
    if (error)
        return error;

    error = pdf_loadoutline(&g_outline, g_xref);
    if (error)
        return error;

    
    return error;
}

void
closefile(void)
{
    if (g_srcpages)
    {
        pdf_droppagetree(g_srcpages);
        g_srcpages = NULL;
    }

    if (g_xref)
    {
        if (g_xref->store)
        {
            pdf_dropstore(g_xref->store);
            g_xref->store = NULL;
        }
        pdf_closexref(g_xref);
        g_xref = NULL;
    }
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
    printf("<%s : bbox = %.2f %.2f %.2f %.2f>\n", 
        kind, node->bbox.x0, node->bbox.x1, 
        node->bbox.y0, node->bbox.y1);

    for (child = node->first; child; child = child->next)
		bbdump(child, level + 1);
}

fz_error* 
renderfile(void)
{
    fz_error    *error;
    fz_renderer *renderer;
    fz_obj      *pageobj;
    pdf_page    *page;
    fz_pixmap   *pix;
    fz_matrix   ctm;
    fz_irect    bbox;
    float       zoom = 1;
    int         rotate = 0;
    int         width, height;
    fz_rect     boundrect;

    // Create a new rendering object
    error = fz_newrenderer(&renderer, pdf_devicergb, 0, 1024 * 512);
    if (error)
        return error;

    // Get the first page and load it
    pageobj = pdf_getpageobject(g_srcpages, 0);
    error = pdf_loadpage(&page, g_xref, pageobj);
    if (error)
        return error;

    // create the matrix
    ctm = fz_identity();
    ctm = fz_concat(ctm, fz_translate(0, - page->mediabox.y1));
    ctm = fz_concat(ctm, fz_scale(zoom, -zoom));
    ctm = fz_concat(ctm, fz_rotate(rotate + page->rotate));

    // get the bounding box
    bbox = fz_roundrect(fz_transformaabb(ctm, page->mediabox));
    width = bbox.x1 - bbox.x0;
    height = bbox.y1 - bbox.y0;

    // this gives us the bounding box for the entire page
    boundrect = fz_boundnode(page->tree->root, ctm);
    bbdump(page->tree->root, 1);

    //// Create a pixel map
    //error = fz_newpixmap(&pix, bbox.x0, bbox.y0, width, height, 4);
    //if (error)
    //    return error;
    //memset(pix->samples, 0xff, pix->h * pix->w * pix->n);

    //// render the tree
    //error = fz_rendertreeover(renderer, pix, page->tree, ctm);
    //if (error)
    //    return error;

    //fz_droppixmap(pix);

    pdf_droppage(page);
    fz_droprenderer(renderer);

    return error;
}


int _tmain(int argc, _TCHAR* argv[])
{
    openfile("d:\\temp\\test.pdf", "");
    renderfile();
    closefile();
	return 0;
}

