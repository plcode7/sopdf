#pragma once

enum EMode
{
    Fit2xWidth,
    Fit2xHeight,
    FitWidth,
    FitHeight,
    SmartFitWidth,
    SmartFitHeight
};

//
// declarations for parameters
//
extern bool     p_cropWhiteSpace;
extern double   p_overlap;
extern EMode    p_mode;

#define SO_PDF_VER  "0.1 alpha Rev 9"

int soPdfError(fz_error *error);