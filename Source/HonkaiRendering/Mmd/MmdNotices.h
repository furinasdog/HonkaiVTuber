#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FMmdNotice
{
    FString Title, Text;
    bool bAvailable = true;
};

struct FMmdFirstRunState
{
    FString AcceptedHash;
    bool bGuideSeen = false;
    bool Load(const FString& Path);
    bool Save(const FString& Path, FString& Error) const;
};

namespace MmdNotices
{
    TArray<FMmdNotice> Software();
    FString Fingerprint(const TArray<FMmdNotice>& Notices);
    FMmdNotice Model(const FString& Root);
}

struct FMmdNoticeReadTime
{
    float Seconds = 0;
    void Advance(float Delta, bool bVisibleAndForeground)
    {
        if (bVisibleAndForeground && FMath::IsFinite(Delta)) Seconds += FMath::Clamp(Delta, 0.f, .25f);
    }
    bool Ready() const { return Seconds >= 5.f; }
};

class SMmdNoticePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMmdNoticePanel) {}
        SLATE_ARGUMENT(FMmdNotice, Notice)
        SLATE_ARGUMENT(bool, RequireFiveSeconds)
        SLATE_ARGUMENT(FString, Folder)
        SLATE_ARGUMENT(TFunction<void()>, Accepted)
        SLATE_ARGUMENT(TFunction<void()>, Declined)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual void Tick(const FGeometry&, double, float Delta) override;
    void SetError(const FString& InError) { Error = InError; }
private:
    FMmdNoticeReadTime ReadTime;
    bool bChecked = false, bRequireTime = false, bAvailable = true;
    FString Error;
};
