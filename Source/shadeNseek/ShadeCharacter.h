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
enum class EShadeAction : uint8;

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
	/** 1 = full shadow, 0 = gone. Drains slowly over time and fast in sunlight; it never refills. */
	float GetHealth() const { return Health; }
	bool IsFaded() const { return bFaded; }
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
	UPoseableMeshComponent* GetBody() const { return Body; }
	UCameraComponent* GetCamera() const { return FollowCamera; }
	USpringArmComponent* GetBoom() const { return CameraBoom; }

	UPROPERTY(EditAnywhere, Category = "Shade")
	float WalkSpeed = 110.f;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float SprintSpeed = 270.f;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float PushSpeed = 95.f;

	/** Seconds of full exposure until Shade evaporates. */
	UPROPERTY(EditAnywhere, Category = "Shade")
	float BurnTime = 0.36f;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float CoolTime = 0.8f;

	/** Shadow lost per second just by being out in the afternoon heat (the walk has a time limit). */
	UPROPERTY(EditAnywhere, Category = "Shade")
	float IdleDrain = 0.0040f;

	/** Shadow lost per second in full sunlight (on top of the burn-up exposure). */
	UPROPERTY(EditAnywhere, Category = "Shade")
	float SunDrain = 0.2f;

	/** Shadow lost each time Shade evaporates. */
	UPROPERTY(EditAnywhere, Category = "Shade")
	float EvaporateCost = 0.18f;

protected:
	void UpdateSun(float Dt);
	void UpdateFocus(float Dt);
	void UpdateGrab(float Dt);
	void UpdateHold(float Dt);
	/** Physical interactions: walk up, face it, rear up, paws on, push / tug / swat, let go (see StartAction). */
	void UpdateAction(float Dt);
	void StartAction(AShadeInteractable* Target);
	/** Body-led locomotion: Shade turns toward where it is asked to go (quickly when slow, in wider arcs at a trot, only a
	 *  little in the air) and runs where its body points, pivoting on the spot instead of sliding sideways. */
	void Locomote(const FVector2D& Axis, float Dt);
	/** A hop that just misses a ledge: the front paws hook the edge and Shade scrambles up and over. */
	bool TryScramble();
	void UpdateScramble(float Dt);
	void EndScramble();
	/** Standing still in the shade for a while, Shade sits down; it gets up the moment it is asked to go. */
	void UpdateIdle(float Dt);
	/** Jump pressed in the air facing a wall: forepaws scrabble up it, hind paws kick off it for more height (up to two
	 *  kicks per leap - a cat bounding up a wall to a ledge). */
	bool TryWallKick();
	void UpdateWallKick(float Dt);
	void BeginLet();
	void CancelAction();
	/** Movement and new interactions are locked while the paws are working (not while letting go). */
	bool IsActBusy() const;
	void UpdateEvaporation(float Dt);
	void UpdateVisuals(float Dt);
	void UpdateWisps(float Dt);
	void UpdateCamera(float Dt);
	void Evaporate();
	void Fade();
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
	TObjectPtr<UMaterialInstanceDynamic> WhiskerMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShellMID;

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

	// Interaction animation state
	enum class EActStage : uint8 { None, Approach, Reach, Work, Let };
	TWeakObjectPtr<AShadeInteractable> ActTarget;
	EShadeAction ActStyle{};
	EActStage ActStage = EActStage::None;
	float ActTime = 0.f;        // seconds in the current stage
	float ActStuck = 0.f;       // seconds the approach made no progress
	float ActW = 0.f;           // eased weight of the paw layer fed to the rig
	float ActPull = 0.f;        // cm the paws have pulled a cord handle down
	float ActLean = 0.f;
	float ActRear = 0.f;
	float ActRearFix = 0.f;     // extra rear when the paws come up short of a high handle
	float ActCycle = 0.f;       // tug rhythm phase
	float CartSpeed = 0.f;      // cm/s along the cart axis: a loaded cart takes a moment to get going and rolls on a little
	bool bActFired = false;     // the interactable's Interact() has been called (paws on it)
	FVector ActSpot = FVector::ZeroVector;
	FVector ActFace = FVector::ForwardVector;
	FVector ActContact = FVector::ZeroVector;
	FVector2D PendingMove = FVector2D::ZeroVector;

	bool bControlEnabled = false;
	bool bSprint = false;
	bool bInvulnerable = false;
	float JumpWindup = 0.f;

	// Locomotion feel
	float LandRecover = 0.f;      // seconds of slowed legs after a hard landing
	float IdleTime = 0.f;         // seconds standing still with nothing asked of it
	bool bSitting = false;
	float JumpWindupTotal = 0.11f;  // length of the current wind-up (shorter on the run)
	int32 WallKicks = 0;          // kicks used since leaving the ground
	float KickT = 10.f;           // seconds since the last wall kick
	FVector KickWall = FVector::ZeroVector;   // a point on the wall (world)
	FVector KickN = FVector::ForwardVector;   // its normal, horizontal
	bool bScrambling = false;
	float ScrambleT = 0.f;        // 0..1 through the scramble
	float ScrambleCooldown = 0.f;
	FVector ScrambleFrom = FVector::ZeroVector;
	FVector ScrambleTo = FVector::ZeroVector;
	FVector ScrambleEdge = FVector::ZeroVector;  // top of the edge the paws hook (world)
	FVector ScrambleFace = FVector::ForwardVector;

	// Sun
	float Exposure = 0.f;
	float Health = 1.f;
	bool bFaded = false;
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
