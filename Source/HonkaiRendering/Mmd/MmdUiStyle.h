#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

struct FMmdUiColors
{
    FLinearColor Panel, Header, Field, Control, Text, Muted, Accent, AccentText, Border, Hover, Pressed, Grid, Warning, Canvas;
    FMmdUiColors();
};

struct FMmdUiStyle
{
    static const FMmdUiStyle& Get();
    static const FMmdUiColors& Colors();
    FButtonStyle Button, Primary;
    FEditableTextBoxStyle Editable;
    FSpinBoxStyle Spin;
    FComboBoxStyle Combo;
    FSliderStyle Slider;
    FCheckBoxStyle Check;
    FTableRowStyle Row;
private:
    FMmdUiStyle();
};
