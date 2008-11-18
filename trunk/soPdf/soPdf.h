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
extern bool     p_proceedWithErrors;

#define SO_PDF_VER  "0.1 alpha Rev 10"

int soPdfError(fz_error *error);
fz_error* soPdfErrorList(fz_error *error);