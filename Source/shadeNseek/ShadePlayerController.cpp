#include "ShadePlayerController.h"
#include "shadeNseek.h"
#include "ShadeAssets.h"
#include "ShadeAutoplayComponent.h"
#include "ShadeCharacter.h"
#include "ShadeGameMode.h"
#include "ShadeHUD.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Engine/LocalPlayer.h"
#include "Camera/PlayerCameraManager.h"

AShadePlayerController::AShadePlayerController()
{
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = CreateDefaultSubobject<UInputAction>(Name);
		A->ValueType = Type;
		return A;
	};
	IA_Move = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	IA_Look = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	IA_Jump = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	IA_Sprint = MakeAction(TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	IA_Interact = MakeAction(TEXT("IA_Interact"), EInputActionValueType::Boolean);
	IA_Pause = MakeAction(TEXT("IA_Pause"), EInputActionValueType::Boolean);
	IA_Pause->bTriggerWhenPaused = true;
	bShowMouseCursor = false;
}

void AShadePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ViewPitchMin = -65.f;
		PlayerCameraManager->ViewPitchMax = 30.f;
	}

	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Shade"));
	auto Swizzle = [this]() { return NewObject<UInputModifierSwizzleAxis>(this); };
	auto Negate = [this]() { return NewObject<UInputModifierNegate>(this); };
	auto Deadzone = [this]() { UInputModifierDeadZone* D = NewObject<UInputModifierDeadZone>(this); D->LowerThreshold = 0.18f; return D; };

	// WASD / arrows -> 2D (X = right, Y = forward)
	for (const FKey& K : { EKeys::D, EKeys::Right }) { Mapping->MapKey(IA_Move, K); }
	for (const FKey& K : { EKeys::A, EKeys::Left }) { Mapping->MapKey(IA_Move, K).Modifiers.Add(Negate()); }
	for (const FKey& K : { EKeys::W, EKeys::Up }) { Mapping->MapKey(IA_Move, K).Modifiers.Add(Swizzle()); }
	for (const FKey& K : { EKeys::S, EKeys::Down })
	{
		FEnhancedActionKeyMapping& S = Mapping->MapKey(IA_Move, K);
		S.Modifiers.Add(Swizzle());
		S.Modifiers.Add(Negate());
	}
	Mapping->MapKey(IA_Move, EKeys::Gamepad_Left2D).Modifiers.Add(Deadzone());
	{
		FEnhancedActionKeyMapping& M = Mapping->MapKey(IA_Look, EKeys::Mouse2D);
		UInputModifierNegate* NegY = Negate();
		NegY->bX = false;
		NegY->bZ = false;
		M.Modifiers.Add(NegY);
	}
	{
		FEnhancedActionKeyMapping& G = Mapping->MapKey(IA_Look, EKeys::Gamepad_Right2D);
		G.Modifiers.Add(Deadzone());
		UInputModifierNegate* NegY = Negate();
		NegY->bX = false;
		NegY->bZ = false;
		G.Modifiers.Add(NegY);
	}
	Mapping->MapKey(IA_Jump, EKeys::SpaceBar);
	Mapping->MapKey(IA_Jump, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(IA_Sprint, EKeys::LeftShift);
	Mapping->MapKey(IA_Sprint, EKeys::Gamepad_LeftThumbstick);
	Mapping->MapKey(IA_Interact, EKeys::E);
	Mapping->MapKey(IA_Interact, EKeys::LeftMouseButton);
	Mapping->MapKey(IA_Interact, EKeys::Gamepad_FaceButton_Left);
	Mapping->MapKey(IA_Pause, EKeys::Escape);
	Mapping->MapKey(IA_Pause, EKeys::P);
	Mapping->MapKey(IA_Pause, EKeys::Gamepad_Special_Right);

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Sub->AddMappingContext(Mapping, 0);
		}
	}

	if (AShadeGameMode::IsAutoplay())
	{
		Autoplay = NewObject<UShadeAutoplayComponent>(this, TEXT("Autoplay"));
		Autoplay->RegisterComponent();
		UE_LOG(LogShade, Log, TEXT("Autoplay enabled"));
	}
}

void AShadePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogShade, Error, TEXT("Enhanced Input component missing - check DefaultInput.ini"));
		return;
	}
	EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AShadePlayerController::OnMove);
	EIC->BindAction(IA_Look, ETriggerEvent::Triggered, this, &AShadePlayerController::OnLook);
	EIC->BindAction(IA_Jump, ETriggerEvent::Started, this, &AShadePlayerController::OnJump);
	EIC->BindAction(IA_Sprint, ETriggerEvent::Started, this, &AShadePlayerController::OnSprintStart);
	EIC->BindAction(IA_Sprint, ETriggerEvent::Completed, this, &AShadePlayerController::OnSprintEnd);
	EIC->BindAction(IA_Interact, ETriggerEvent::Started, this, &AShadePlayerController::OnInteractStart);
	EIC->BindAction(IA_Interact, ETriggerEvent::Completed, this, &AShadePlayerController::OnInteractEnd);
	EIC->BindAction(IA_Interact, ETriggerEvent::Canceled, this, &AShadePlayerController::OnInteractEnd);
	EIC->BindAction(IA_Pause, ETriggerEvent::Started, this, &AShadePlayerController::OnPause);
}

bool AShadePlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	if (Params.Event == IE_Pressed)
	{
		bUsingGamepad = Params.Key.IsGamepadKey();
	}
	return Super::InputKey(Params);
}

AShadeCharacter* AShadePlayerController::GetShade() const
{
	return Cast<AShadeCharacter>(GetPawn());
}

void AShadePlayerController::OnMove(const FInputActionValue& Value)
{
	if (Autoplay) { return; }
	if (AShadeCharacter* S = GetShade()) { S->MoveInput(Value.Get<FVector2D>()); }
}

void AShadePlayerController::OnLook(const FInputActionValue& Value)
{
	if (Autoplay) { return; }
	if (AShadeCharacter* S = GetShade())
	{
		FVector2D V = Value.Get<FVector2D>();
		V *= bUsingGamepad ? GamepadLookRate : MouseSensitivity;
		S->LookInput(V);
	}
}

void AShadePlayerController::OnJump(const FInputActionValue&)
{
	if (Autoplay) { return; }
	if (AShadeCharacter* S = GetShade()) { S->JumpInput(); }
}

void AShadePlayerController::OnSprintStart(const FInputActionValue&)
{
	if (Autoplay) { return; }
	if (AShadeCharacter* S = GetShade()) { S->SetSprint(true); }
}

void AShadePlayerController::OnSprintEnd(const FInputActionValue&)
{
	if (Autoplay) { return; }
	if (AShadeCharacter* S = GetShade()) { S->SetSprint(false); }
}

void AShadePlayerController::OnInteractStart(const FInputActionValue&)
{
	if (Autoplay) { return; }
	AShadeGameMode* GM = GetWorld()->GetAuthGameMode<AShadeGameMode>();
	if (GM && GM->GetPhase() == EShadePhase::Title)
	{
		GM->StartGame();
		return;
	}
	if (AShadeCharacter* S = GetShade()) { S->InteractPressed(); }
}

void AShadePlayerController::OnInteractEnd(const FInputActionValue&)
{
	if (Autoplay) { return; }
	if (AShadeCharacter* S = GetShade()) { S->InteractReleased(); }
}

void AShadePlayerController::OnPause(const FInputActionValue&)
{
	AShadeGameMode* GM = GetWorld()->GetAuthGameMode<AShadeGameMode>();
	if (!GM || GM->GetPhase() == EShadePhase::Title || GM->GetPhase() == EShadePhase::Ended)
	{
		return;
	}
	if (bPauseMenu) { ClosePauseMenu(); }
	else { OpenPauseMenu(); }
}

void AShadePlayerController::OpenPauseMenu()
{
	if (bPauseMenu) { return; }
	bPauseMenu = true;
	SetPause(true);
	ShadeAssets::Play2D(this, TEXT("SFX_UI_Open"), 0.6f);
	if (AShadeHUD* HUD = GetHUD<AShadeHUD>()) { HUD->ShowMenu(EShadeMenu::Pause); }
	ApplyMenuInputMode(true);
}

void AShadePlayerController::ClosePauseMenu()
{
	if (!bPauseMenu) { return; }
	bPauseMenu = false;
	SetPause(false);
	ShadeAssets::Play2D(this, TEXT("SFX_UI_Close"), 0.6f);
	if (AShadeHUD* HUD = GetHUD<AShadeHUD>()) { HUD->ShowMenu(EShadeMenu::None); }
	ApplyMenuInputMode(false);
}

void AShadePlayerController::OnPhaseChanged(EShadePhase Phase)
{
	AShadeHUD* HUD = GetHUD<AShadeHUD>();
	switch (Phase)
	{
	case EShadePhase::Title:
		if (HUD) { HUD->ShowMenu(EShadeMenu::Title); }
		ApplyMenuInputMode(true);
		break;
	case EShadePhase::Ended:
		if (HUD) { HUD->ShowMenu(EShadeMenu::End); }
		ApplyMenuInputMode(true);
		break;
	default:
		if (HUD && !bPauseMenu) { HUD->ShowMenu(EShadeMenu::None); }
		if (!bPauseMenu) { ApplyMenuInputMode(false); }
		break;
	}
}

void AShadePlayerController::ApplyMenuInputMode(bool bMenu)
{
	AShadeHUD* HUD = GetHUD<AShadeHUD>();
	if (bMenu)
	{
		FInputModeGameAndUI Mode;
		if (HUD && HUD->GetFocusWidget().IsValid())
		{
			Mode.SetWidgetToFocus(HUD->GetFocusWidget());
		}
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		bShowMouseCursor = !Autoplay;
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}
