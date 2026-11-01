#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"

struct FMmdUiStyle
{
    static const FMmdUiStyle& Get();
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
