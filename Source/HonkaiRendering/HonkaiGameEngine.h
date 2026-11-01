#pragma once

#include "CoreMinimal.h"
#include "Engine/GameEngine.h"
#include "HonkaiGameEngine.generated.h"

UCLASS()
class HONKAIRENDERING_API UHonkaiGameEngine : public UGameEngine
{
    GENERATED_BODY()

public:
    virtual TSharedRef<SViewport> CreateGameViewportWidget() override;
};
