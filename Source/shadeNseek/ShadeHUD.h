#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ShadeHUD.generated.h"

class SShadeOverlay;
class SWidget;
class SOverlay;
struct FStandaloneCompositeFont;

UENUM()
enum class EShadeMenu : uint8
{
	None,
	Title,
	Pause,
	End
};

/**
 * Hosts the custom-painted Slate overlay: title, objective tile, prompts, sun-exposure warning, narration, menus.
 * Fonts (OFL) are loaded from Content/Shade/UI/Fonts as raw files.
 */
UCLASS()
class SHADENSEEK_API AShadeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	void ShowMenu(EShadeMenu NewMenu);
	EShadeMenu GetMenu() const { return Menu; }
	double GetMenuOpenTime() const { return MenuOpenTime; }
	int32 GetMenuItemCount() const;
	FText GetMenuItemLabel(int32 Index) const;
	void ActivateMenuItem(int32 Index);
	TSharedPtr<SWidget> GetFocusWidget() const;

	int32 Selected = 0;

	/** Faces: "Display"/"DisplayItalic" (Fraunces SemiBold / SemiBold Italic), "Body"/"BodyBold"/"BodyHeavy" (Nunito SemiBold / Bold / ExtraBold). Missing files fall back to Roboto. */
	FSlateFontInfo Font(FName Face, float Size, int32 LetterSpacing = 0) const;

private:
	TSharedPtr<SOverlay> Root;
	TSharedPtr<SShadeOverlay> Overlay;
	TMap<FName, TSharedPtr<FStandaloneCompositeFont>> Fonts;
	EShadeMenu Menu = EShadeMenu::None;
	double MenuOpenTime = 0.0;
};
