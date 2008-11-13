#pragma once

enum EScale
{
    FitToHeight,
    FitToWidth,
    FitTo2xHeight,
    FitTo2xWidth,
    FitTo3xWidth
};

//
// declarations for parameters
//
extern char*   p_szInFile;
extern char*   p_szOutFile;
extern char*   p_szTitle;
extern char*   p_szAuthor;
extern char*   p_szPublisher;
extern char*   p_szCategory;
extern char*   p_noPages;

extern bool    p_cropSides;
extern double  p_overlap;
extern EScale  p_scale;


int soPdfError(fz_error *error);