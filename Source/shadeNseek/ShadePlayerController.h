#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ShadeTypes.h"
#include "ShadePlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UShadeAutoplayComponent;
class AShadeCharacter;
struct FInputActionValue;

/**
 * Enhanced Input set up entirely in code (keyboard/mouse + gamepad), forwarded to Shade.
 * With -autoplay, player input is ignored and UShadeAutoplayComponent drives the same Shade API.
 */
UCLASS()
class SHADENSEEK_API AShadePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AShadePlayerController();

	void OnPhaseChanged(EShadePhase Phase);
	void OpenPauseMenu();
	void ClosePauseMenu();
	bool IsPauseMenuOpen() const { return bPauseMenu; }
	bool IsAutoplaying() const { return Autoplay != nullptr; }
	bool IsUsingGamepad() const { return bUsingGamepad; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	void ApplyMenuInputMode(bool bMenu);
	AShadeCharacter* GetShade() const;

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJump(const FInputActionValue& Value);
	void OnSprintStart(const FInputActionValue& Value);
	void OnSprintEnd(const FInputActionValue& Value);
	void OnInteractStart(const FInputActionValue& Value);
	void OnInteractEnd(const FInputActionValue& Value);
	void OnPause(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, Category = "Input") TObjectPtr<UInputAction> IA_Move;
	UPROPERTY(VisibleAnywhere, Category = "Input") TObjectPtr<UInputAction> IA_Look;
	UPROPERTY(VisibleAnywhere, Category = "Input") TObjectPtr<UInputAction> IA_Jump;
	UPROPERTY(VisibleAnywhere, Category = "Input") TObjectPtr<UInputAction> IA_Sprint;
	UPROPERTY(VisibleAnywhere, Category = "Input") TObjectPtr<UInputAction> IA_Interact;
	UPROPERTY(VisibleAnywhere, Category = "Input") TObjectPtr<UInputAction> IA_Pause;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UShadeAutoplayComponent> Autoplay;

	UPROPERTY(EditAnywhere, Category = "Input") float MouseSensitivity = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Input") float GamepadLookRate = 2.4f;

	bool bPauseMenu = false;
	bool bUsingGamepad = false;
};
