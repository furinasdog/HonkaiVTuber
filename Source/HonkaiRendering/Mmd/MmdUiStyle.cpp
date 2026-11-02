#include "MmdUiStyle.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

FMmdUiColors::FMmdUiColors()
{
    const auto Color = [](uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); };
    Panel = Color(48, 48, 48); Header = Color(38, 38, 38); Field = Color(36, 36, 36); Control = Color(61, 61, 61);
    Text = Color(222, 222, 222); Muted = Color(172, 172, 172); Accent = Color(71, 114, 179);
    AccentText = Color(142, 181, 231);
    Border = Color(83, 83, 83); Hover = Color(75, 75, 75); Pressed = Color(48, 76, 115);
    Grid = Color(66, 66, 66); Warning = Color(230, 180, 98); Canvas = Color(30, 30, 30);
}

const FMmdUiColors& FMmdUiStyle::Colors()
{
    static const FMmdUiColors Palette;
    return Palette;
}

const FMmdUiStyle& FMmdUiStyle::Get()
{
    static const FMmdUiStyle Style;
    return Style;
}

FMmdUiStyle::FMmdUiStyle()
{
    const auto Color = [](uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); };
    const auto& Palette = Colors();
    const FLinearColor Ink = Palette.Text, Muted = Palette.Muted, Accent = Palette.Accent, Field = Palette.Field;
    const FLinearColor Border = Palette.Border, Hover = Palette.Hover, Pressed = Palette.Pressed;
    Button = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
    Button.SetNormal(FSlateRoundedBoxBrush(Palette.Control, 3.f, Border, 1.f))
        .SetHovered(FSlateRoundedBoxBrush(Hover, 3.f, Accent, 1.f))
        .SetPressed(FSlateRoundedBoxBrush(Pressed, 3.f, Accent, 1.f))
        .SetDisabled(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetNormalForeground(Ink).SetHoveredForeground(Ink).SetPressedForeground(Ink).SetDisabledForeground(Muted);
    Primary = Button;
    Primary.SetNormal(FSlateRoundedBoxBrush(Accent, 3.f))
        .SetHovered(FSlateRoundedBoxBrush(Color(75, 120, 180), 3.f))
        .SetPressed(FSlateRoundedBoxBrush(Color(54, 91, 146), 3.f))
        .SetNormalForeground(FLinearColor::White).SetHoveredForeground(FLinearColor::White)
        .SetPressedForeground(FLinearColor::White).SetDisabledForeground(Muted);
    Editable = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
    Editable.SetBackgroundImageNormal(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetBackgroundImageHovered(FSlateRoundedBoxBrush(Field, 3.f, Accent, 1.f))
        .SetBackgroundImageFocused(FSlateRoundedBoxBrush(Field, 3.f, Accent, 1.f))
        .SetBackgroundImageReadOnly(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetBackgroundColor(FLinearColor::White).SetForegroundColor(Ink)
        .SetFocusedForegroundColor(Ink).SetReadOnlyForegroundColor(Muted).SetPadding(FMargin(9, 7));
    Editable.TextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 12)).SetColorAndOpacity(Ink);
    Spin = FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox");
    Spin.SetBackgroundBrush(FSlateRoundedBoxBrush(Field, 3.f, Border, 1.f))
        .SetActiveBackgroundBrush(FSlateRoundedBoxBrush(Field, 3.f, Accent, 1.f))
        .SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(Field, 3.f, Accent, 1.f))
        .SetInactiveFillBrush(FSlateColorBrush(FLinearColor::Transparent))
        .SetHoveredFillBrush(FSlateColorBrush(Hover)).SetActiveFillBrush(FSlateColorBrush(Pressed))
        .SetForegroundColor(Ink);
    Combo = FCoreStyle::Get().GetWidgetStyle<FComboBoxStyle>("ComboBox");
    Combo.ComboButtonStyle.SetButtonStyle(Button)
        .SetMenuBorderBrush(FSlateRoundedBoxBrush(Palette.Panel, 3.f, Border, 1.f)).SetMenuBorderPadding(FMargin(4));
    Combo.ComboButtonStyle.DownArrowImage.TintColor = Ink;
    FSlateColorBrush Thumb(Accent), DisabledThumb(Border);
    Thumb.SetImageSize(FVector2f(12, 12)); DisabledThumb.SetImageSize(FVector2f(12, 12));
    Slider = FCoreStyle::Get().GetWidgetStyle<FSliderStyle>("Slider");
    Slider.SetNormalBarImage(FSlateColorBrush(Border)).SetHoveredBarImage(FSlateColorBrush(Border))
        .SetDisabledBarImage(FSlateColorBrush(Palette.Grid))
        .SetNormalThumbImage(Thumb).SetHoveredThumbImage(Thumb).SetDisabledThumbImage(DisabledThumb).SetBarThickness(3.f);
    FSlateColorBrush EmptyCheck(FLinearColor::Transparent); EmptyCheck.SetImageSize(FVector2f(16, 16));
    FSlateBrush Mark = *FCoreStyle::Get().GetBrush("EditableComboBox.Accept"); Mark.TintColor = Accent;
    Check = FCoreStyle::Get().GetWidgetStyle<FCheckBoxStyle>("Checkbox");
    Check.SetBackgroundImage(FSlateRoundedBoxBrush(Field, 2.f, Border, 1.f, FVector2f(16, 16)))
        .SetBackgroundHoveredImage(FSlateRoundedBoxBrush(Field, 2.f, Accent, 1.f, FVector2f(16, 16)))
        .SetBackgroundPressedImage(FSlateRoundedBoxBrush(Field, 2.f, Accent, 1.f, FVector2f(16, 16)))
        .SetUncheckedImage(EmptyCheck).SetUncheckedHoveredImage(EmptyCheck).SetUncheckedPressedImage(EmptyCheck)
        .SetCheckedImage(Mark).SetCheckedHoveredImage(Mark).SetCheckedPressedImage(Mark);
    Row = FCoreStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
    Row.SetTextColor(Ink).SetSelectedTextColor(Ink)
        .SetEvenRowBackgroundBrush(FSlateColorBrush(Palette.Panel)).SetOddRowBackgroundBrush(FSlateColorBrush(Palette.Panel))
        .SetEvenRowBackgroundHoveredBrush(FSlateColorBrush(Hover)).SetOddRowBackgroundHoveredBrush(FSlateColorBrush(Hover))
        .SetActiveBrush(FSlateColorBrush(Pressed)).SetActiveHoveredBrush(FSlateColorBrush(Pressed))
        .SetInactiveBrush(FSlateColorBrush(Hover)).SetInactiveHoveredBrush(FSlateColorBrush(Pressed));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdDarkThemeTest, "HonkaiRendering.UI.DarkThemeContrast",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdDarkThemeTest::RunTest(const FString& Parameters)
{
    const auto& C = FMmdUiStyle::Colors();
    const auto Luminance = [](FLinearColor Color) { return .2126f * Color.R + .7152f * Color.G + .0722f * Color.B; };
    const auto Contrast = [&](FLinearColor A, FLinearColor B)
    { return (FMath::Max(Luminance(A), Luminance(B)) + .05f) / (FMath::Min(Luminance(A), Luminance(B)) + .05f); };
    TestTrue(TEXT("Body text contrasts with the Blender grey panel"), Contrast(C.Text, C.Panel) >= 4.5f);
    TestTrue(TEXT("Secondary labels remain readable"), Contrast(C.Muted, C.Panel) >= 4.5f);
    TestTrue(TEXT("Input text contrasts with its field"), Contrast(C.Text, C.Field) >= 4.5f);
    TestTrue(TEXT("Colored status labels remain readable"), Contrast(C.AccentText, C.Panel) >= 4.5f);
    TestTrue(TEXT("Primary action text remains readable"), Contrast(FLinearColor::White, C.Accent) >= 4.5f);
    const auto& Style = FMmdUiStyle::Get();
    TestTrue(TEXT("Primary hover state retains readable text"), Contrast(FLinearColor::White, Style.Primary.Hovered.TintColor.GetSpecifiedColor()) >= 4.5f);
    TestTrue(TEXT("Buttons share the dark control surface"), Style.Button.Normal.TintColor.GetSpecifiedColor().Equals(C.Control));
    TestTrue(TEXT("Editable fields share the dark field surface"), Style.Editable.BackgroundImageNormal.TintColor.GetSpecifiedColor().Equals(C.Field));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMmdWindowDefaultsTest, "HonkaiRendering.UI.WindowDefaultsRespectUser",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMmdWindowDefaultsTest::RunTest(const FString& Parameters)
{
    const TCHAR* Section = TEXT("/Script/Engine.GameUserSettings");
    FConfigFile Settings;
    Settings.Read(FPaths::ProjectConfigDir() / TEXT("DefaultGameUserSettings.ini"));
    int32 Width = 0, Height = 0, Mode = -1;
    TestTrue(TEXT("Default window width exists"), Settings.GetInt(Section, TEXT("ResolutionSizeX"), Width));
    TestTrue(TEXT("Default window height exists"), Settings.GetInt(Section, TEXT("ResolutionSizeY"), Height));
    TestTrue(TEXT("Default window mode exists"), Settings.GetInt(Section, TEXT("FullscreenMode"), Mode));
    TestEqual(TEXT("First launch has a useful window width"), Width, 1280);
    TestEqual(TEXT("First launch has a useful window height"), Height, 800);
    TestEqual(TEXT("First launch uses a movable window"), Mode, 2);
    Settings.CombineFromBuffer(TEXT("[/Script/Engine.GameUserSettings]\nResolutionSizeX=1600\nResolutionSizeY=900\nFullscreenMode=1\n"), TEXT("SavedUserPreferencesFixture.ini"));
    Settings.GetInt(Section, TEXT("ResolutionSizeX"), Width);
    Settings.GetInt(Section, TEXT("ResolutionSizeY"), Height);
    Settings.GetInt(Section, TEXT("FullscreenMode"), Mode);
    TestEqual(TEXT("A later saved width overrides the default"), Width, 1600);
    TestEqual(TEXT("A later saved height overrides the default"), Height, 900);
    TestEqual(TEXT("A later saved window mode overrides the default"), Mode, 1);
    return true;
}
#endif
