#include "MmdUiStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

const FMmdUiStyle& FMmdUiStyle::Get()
{
    static const FMmdUiStyle Style;
    return Style;
}

FMmdUiStyle::FMmdUiStyle()
{
    const auto Color = [](uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); };
    const FLinearColor Ink = Color(32, 41, 56), Muted = Color(102, 117, 138), Accent = Color(71, 116, 170), Field = Color(248, 250, 253);
    const FLinearColor Border = Color(193, 204, 218), Hover = Color(238, 243, 250), Pressed = Color(221, 232, 245);
    Button = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    Button.SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 3.f, Border, 1.f))
        .SetHovered(FSlateRoundedBoxBrush(Hover, 3.f, Accent, 1.f))
        .SetPressed(FSlateRoundedBoxBrush(Pressed, 3.f, Accent, 1.f))
        .SetDisabled(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetNormalForeground(Ink).SetHoveredForeground(Ink).SetPressedForeground(Ink).SetDisabledForeground(Muted);
    Primary = Button;
    Primary.SetNormal(FSlateRoundedBoxBrush(Accent, 3.f))
        .SetHovered(FSlateRoundedBoxBrush(Color(58, 103, 158), 3.f))
        .SetPressed(FSlateRoundedBoxBrush(Color(43, 86, 138), 3.f))
        .SetNormalForeground(FLinearColor::White).SetHoveredForeground(FLinearColor::White)
        .SetPressedForeground(FLinearColor::White).SetDisabledForeground(Muted);
    Editable = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
    Editable.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetBackgroundImageHovered(FSlateRoundedBoxBrush(FLinearColor::White, 3.f, Accent, 1.f))
        .SetBackgroundImageFocused(FSlateRoundedBoxBrush(FLinearColor::White, 3.f, Accent, 1.f))
        .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetBackgroundColor(FLinearColor::White).SetForegroundColor(Ink)
        .SetFocusedForegroundColor(Ink).SetReadOnlyForegroundColor(Muted).SetPadding(FMargin(9, 7));
    Editable.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12)).SetColorAndOpacity(Ink);
    Spin = FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");
    Spin.SetBackgroundBrush(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetActiveBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor::White, 3.f, Accent, 1.f))
        .SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor::White, 3.f, Accent, 1.f))
        .SetInactiveFillBrush(FSlateColorBrush(FLinearColor::Transparent))
        .SetHoveredFillBrush(FSlateColorBrush(Hover)).SetActiveFillBrush(FSlateColorBrush(Pressed))
        .SetForegroundColor(Ink);
    Combo = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox");
    Combo.ComboButtonStyle.SetButtonStyle(Button)
        .SetMenuBorderBrush(FSlateRoundedBoxBrush(FLinearColor::White, 3.f, Border, 1.f)).SetMenuBorderPadding(FMargin(4));
    FSlateColorBrush Thumb(Accent), DisabledThumb(Border);
    Thumb.SetImageSize(FVector2f(12, 12)); DisabledThumb.SetImageSize(FVector2f(12, 12));
    Slider = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
    Slider.SetNormalBarImage(FSlateColorBrush(Border)).SetHoveredBarImage(FSlateColorBrush(Border))
        .SetDisabledBarImage(FSlateColorBrush(Color(230, 235, 242)))
        .SetNormalThumbImage(Thumb).SetHoveredThumbImage(Thumb).SetDisabledThumbImage(DisabledThumb).SetBarThickness(3.f);
    FSlateColorBrush EmptyCheck(FLinearColor::Transparent); EmptyCheck.SetImageSize(FVector2f(16, 16));
    FSlateBrush Mark = *FCoreStyle::Get().GetBrush("EditableComboBox.Accept"); Mark.TintColor = Accent;
    Check = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
    Check.SetBackgroundImage(FSlateRoundedBoxBrush(FLinearColor::White, 2.f, Border, 1.f, FVector2f(16, 16)))
        .SetBackgroundHoveredImage(FSlateRoundedBoxBrush(Field, 2.f, Accent, 1.f, FVector2f(16, 16)))
        .SetBackgroundPressedImage(FSlateRoundedBoxBrush(Field, 2.f, Accent, 1.f, FVector2f(16, 16)))
        .SetUncheckedImage(EmptyCheck).SetUncheckedHoveredImage(EmptyCheck).SetUncheckedPressedImage(EmptyCheck)
        .SetCheckedImage(Mark).SetCheckedHoveredImage(Mark).SetCheckedPressedImage(Mark);
    Row = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
    Row.SetTextColor(Ink).SetSelectedTextColor(Ink)
        .SetEvenRowBackgroundBrush(FSlateColorBrush(FLinearColor::White)).SetOddRowBackgroundBrush(FSlateColorBrush(FLinearColor::White))
        .SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(Hover)).SetOddRowBackgroundHoveredBrush(FSlateColorBrush(Hover))
        .SetActiveBrush(FSlateColorBrush(Pressed)).SetActiveHoveredBrush(FSlateColorBrush(Pressed))
        .SetInactiveBrush(FSlateColorBrush(Hover)).SetInactiveHoveredBrush(FSlateColorBrush(Pressed));
}
