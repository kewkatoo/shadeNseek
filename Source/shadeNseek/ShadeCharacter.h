#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ShadeTypes.h"
#include "ShadeCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UShadeCatRig;
class AShadeInteractable;
class AShadeLemonCart;

/** One pooled "ink wisp" quad (Shade's trail, burn smoke, evaporation burst). */
struct FShadeWisp
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	float Age = 0.f;
	float Life = 0.f;
	float Size0 = 10.f;
	float Size1 = 20.f;
	float Heat = 0.f;   // 0 ink, 1 ember smoke
	float Alpha = 0.5f;
	bool bActive = false;
};

/**
 * Shade - a detached, living cat-shadow. Direct sunlight is lethal: exposure builds whenever any of three body
 * points can see the sun (UShadeSunSubsystem traces), and at 100% Shade evaporates and re-forms at the last safe
 * patch of shade. Shade can push the lemon cart, tug cords, nudge pots and jump.
 */
UCLASS()
class SHADENSEEK_API AShadeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AShadeCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnJumped_Implementation() override;

	// --- Input-facing API (player controller and autoplay call the same functions) -------------------------------
	void MoveInput(const FVector2D& Axis);
	void LookInput(const FVector2D& Axis);
	void JumpInput();
	void SetSprint(bool bInSprint);
	void InteractPressed(AShadeInteractable* Explicit = nullptr);
	void InteractReleased();
	void ToggleGrab(AShadeLemonCart* Cart);
	void ReleaseGrab();

	// --- State for HUD / game mode ---------------------------------------------------------------------------------
	float GetExposure() const { return Exposure; }
	float GetSunFraction() const { return SunFraction; }
	bool IsEvaporating() const { return EvapState != 0; }
	float GetEvapAlpha() const { return EvapAlpha; }
	int32 GetEvaporations() const { return Evaporations; }
	AShadeInteractable* GetFocused() const { return Focused.Get(); }
	float GetFocusAlpha() const { return FocusAlpha; }
	AShadeLemonCart* GetGrabbed() const { return Grabbed.Get(); }
	AShadeInteractable* GetHoldTarget() const { return HoldTarget.Get(); }
	bool IsSprinting() const { return bSprint; }
	float GetTimeSinceSafe() const { return TimeSinceSafe; }

	void SetControlEnabled(bool bEnabled);
	bool IsControlEnabled() const { return bControlEnabled; }
	void SetForcedFocus(AShadeInteractable* Target);
	void SetInvulnerable(bool bInv) { bInvulnerable = bInv; }

	/** Reunion: Shade pours itself into the spot where the cat's shadow belongs. */
	void BeginMerge(const FVector& Spot, float Duration);
	float GetMergeAlpha() const { return MergeAlpha; }

	void AddTrauma(float Amount) { Trauma = FMath::Min(1.f, Trauma + Amount); }
	void SpawnBurst(const FVector& Center, int32 Count, float Heat, float Speed);

	UShadeCatRig* GetRig() const { return Rig; }
	UCameraComponent* GetCamera() const { return FollowCamera; }
	USpringArmComponent* GetBoom() const { return CameraBoom; }

	UPROPERTY(EditAnywhere, Category = "Shade")
	float WalkSpeed = 330.f;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float SprintSpeed = 560.f;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float PushSpeed = 120.f;

	/** Seconds of full exposure until Shade evaporates. */
	UPROPERTY(EditAnywhere, Category = "Shade")
	float BurnTime = 0.42f;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float CoolTime = 0.7f;

protected:
	void UpdateSun(float Dt);
	void UpdateFocus(float Dt);
	void UpdateGrab(float Dt);
	void UpdateHold(float Dt);
	void UpdateEvaporation(float Dt);
	void UpdateVisuals(float Dt);
	void UpdateWisps(float Dt);
	void UpdateCamera(float Dt);
	void Evaporate();
	void SpawnWisp(const FVector& P, const FVector& V, float Size0, float Size1, float Life, float Heat, float Alpha);
	void GetSunPoints(TArray<FVector>& Out) const;
	class AShadeGameMode* GetShadeGameMode() const;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UPoseableMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UShadeCatRig> Rig;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UPointLightComponent> EyeLight;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WispComps;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EyeMID;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SizzleAudio;

	TArray<FShadeWisp> Wisps;
	TArray<TWeakObjectPtr<AShadeInteractable>> Interactables;

	TWeakObjectPtr<AShadeInteractable> Focused;
	TWeakObjectPtr<AShadeInteractable> ForcedFocus;
	TWeakObjectPtr<AShadeInteractable> HoldTarget;
	TWeakObjectPtr<AShadeLemonCart> Grabbed;
	float FocusAlpha = 0.f;
	float GrabBlend = 0.f;
	FVector2D PendingMove = FVector2D::ZeroVector;

	bool bControlEnabled = false;
	bool bSprint = false;
	bool bInvulnerable = false;

	// Sun
	float Exposure = 0.f;
	float SunFraction = 0.f;
	float TimeSinceSafe = 0.f;
	float SafeSampleTimer = 0.f;
	struct FSafeSpot { FVector Loc; FRotator Rot; double Time; };
	TArray<FSafeSpot> SafeSpots;

	// Evaporation: 0 none, 1 dissolving, 2 gone, 3 re-forming
	int32 EvapState = 0;
	float EvapTimer = 0.f;
	float EvapAlpha = 0.f;
	int32 Evaporations = 0;

	// Merge (reunion)
	bool bMerging = false;
	float MergeAlpha = 0.f;
	float MergeDuration = 2.f;
	FVector MergeSpot = FVector::ZeroVector;

	// Feel
	float Trauma = 0.f;
	float NoiseTime = 0.f;
	float LastYaw = 0.f;
	float WispTimer = 0.f;
	float SmokeTimer = 0.f;
	float BaseFOV = 78.f;
	float CamPitchHelp = 0.f;
	float LookIdle = 0.f;
};
