#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShadeTypes.h"
#include "ShadeInteractables.generated.h"

class AShadeCharacter;
class UStaticMeshComponent;
class UBoxComponent;
class UAudioComponent;
class UPoseableMeshComponent;
class UShadeCatRig;
class UMaterialInstanceDynamic;

/**
 * Anything Shade can use with E. The puzzle objects all cast real shadows that the sun subsystem traces against,
 * so "solving" a puzzle is literally moving shade into place.
 */
UCLASS(Abstract)
class SHADENSEEK_API AShadeInteractable : public AActor
{
	GENERATED_BODY()

public:
	AShadeInteractable();

	/** Short verb for the prompt ("Push", "Tug", "Nudge", "Wake"). */
	UPROPERTY(EditAnywhere, Category = "Shade")
	FText Verb;

	/** What it is ("Lemon cart"). */
	UPROPERTY(EditAnywhere, Category = "Shade")
	FText Label;

	UPROPERTY(EditAnywhere, Category = "Shade")
	float InteractRange = 120.f;

	/** Game-mode story event fired once when the puzzle is solved. */
	UPROPERTY(EditAnywhere, Category = "Shade")
	FName SolvedEvent;

	virtual bool CanInteract(const AShadeCharacter* Shade) const;
	virtual bool IsHoldInteraction() const { return false; }
	virtual void Interact(AShadeCharacter* Shade) {}
	virtual void EndInteract(AShadeCharacter* Shade) {}

	/** Where the prompt floats. */
	virtual FVector GetFocusLocation() const;
	/** Where Shade has to be (range is measured from here). */
	virtual FVector GetInteractLocation() const;
	bool IsSolved() const { return bSolved; }
	/** 0..1 progress for hold interactions (HUD ring). */
	virtual float GetProgress() const { return bSolved ? 1.f : 0.f; }

protected:
	void MarkSolved();

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<USceneComponent> FocusPoint;

	bool bSolved = false;
};

/**
 * A vendor's lemon cart: pushed along its long axis on squeaky wheels. Parked in a sunbeam, its bulk shades the lane.
 */
UCLASS()
class SHADENSEEK_API AShadeLemonCart : public AShadeInteractable
{
	GENERATED_BODY()

public:
	AShadeLemonCart();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Interact(AShadeCharacter* Shade) override;
	virtual FVector GetInteractLocation() const override;
	virtual FVector GetFocusLocation() const override;
	virtual bool CanInteract(const AShadeCharacter* Shade) const override;

	/** Moves the cart along its axis by up to Delta cm (sweeping). Returns the distance actually moved. */
	float Push(float Delta);
	FVector GetPushDirection() const { return GetActorForwardVector(); }
	/** World transform Shade snaps to while pushing. */
	FVector GetGrabLocation() const;
	float GetOffset() const { return Offset; }
	virtual float GetProgress() const override { return FMath::Clamp(Offset / FMath::Max(SolvedOffset, 1.f), 0.f, 1.f); }

	UPROPERTY(EditAnywhere, Category = "Cart")
	float TrackLength = 420.f;

	UPROPERTY(EditAnywhere, Category = "Cart")
	float SolvedOffset = 300.f;

	/** Distance from the cart centre to where Shade braces (behind the cart). */
	UPROPERTY(EditAnywhere, Category = "Cart")
	float GrabDistance = 150.f;

	UPROPERTY(EditAnywhere, Category = "Cart")
	float WheelRadius = 22.f;

	UPROPERTY(VisibleAnywhere, Category = "Cart")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "Cart")
	TObjectPtr<UStaticMeshComponent> CartMesh;

	UPROPERTY(VisibleAnywhere, Category = "Cart")
	TObjectPtr<UStaticMeshComponent> WheelL;

	UPROPERTY(VisibleAnywhere, Category = "Cart")
	TObjectPtr<UStaticMeshComponent> WheelR;

	bool IsGrabbed() const { return bGrabbed; }
	void SetGrabbed(bool bInGrabbed) { bGrabbed = bInGrabbed; }

private:
	FVector StartLocation = FVector::ZeroVector;
	float Offset = 0.f;
	float Velocity = 0.f;
	float LastOffset = 0.f;
	float WheelAngle = 0.f;
	float RattleTimer = 0.f;
	bool bGrabbed = false;
	bool bHitEnd = false;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> RollAudio;
};

/** Café awning on a pull-cord: hold E to tug it open; the canvas shadow sweeps across the piazza. */
UCLASS()
class SHADENSEEK_API AShadeAwning : public AShadeInteractable
{
	GENERATED_BODY()

public:
	AShadeAwning();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsHoldInteraction() const override { return true; }
	virtual void Interact(AShadeCharacter* Shade) override;
	virtual void EndInteract(AShadeCharacter* Shade) override;
	virtual FVector GetInteractLocation() const override;
	virtual FVector GetFocusLocation() const override;
	virtual float GetProgress() const override { return Extension; }
	bool IsTugging() const { return bHolding; }

	/** Projection from the wall (cm, along local +X) when fully open. */
	UPROPERTY(EditAnywhere, Category = "Awning")
	float Depth = 340.f;

	/** Width along local Y (cm). */
	UPROPERTY(EditAnywhere, Category = "Awning")
	float Width = 520.f;

	/** Downward slope of the canvas (deg). */
	UPROPERTY(EditAnywhere, Category = "Awning")
	float Slope = 14.f;

	UPROPERTY(EditAnywhere, Category = "Awning")
	float OpenSpeed = 0.42f;

	/** Cord handle position relative to the actor (on the wall, at cat height). */
	UPROPERTY(EditAnywhere, Category = "Awning")
	FVector CordLocal = FVector(14.f, 300.f, -205.f);

	UPROPERTY(EditAnywhere, Category = "Awning")
	float StartExtension = 0.06f;

	/** Where Shade stands to tug, relative to the cord handle (actor space). */
	UPROPERTY(EditAnywhere, Category = "Awning")
	FVector InteractOffset = FVector(30.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Awning")
	bool bShowArms = true;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> Roller;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> Canvas;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> Valance;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> ArmL;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> ArmR;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> Cord;

	UPROPERTY(VisibleAnywhere, Category = "Awning")
	TObjectPtr<UStaticMeshComponent> Handle;

private:
	void UpdateGeometry();

	float Extension = 0.f;
	float TugPhase = 0.f;
	float ClickAccum = 0.f;
	bool bHolding = false;
	FShadeSpring Flutter;
	float Time = 0.f;
};

/** A big terracotta pot on a wall top. Cats knock things off ledges; this one lands where its leafy shadow matters. */
UCLASS()
class SHADENSEEK_API AShadeFlowerPot : public AShadeInteractable
{
	GENERATED_BODY()

public:
	AShadeFlowerPot();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void Interact(AShadeCharacter* Shade) override;
	virtual FVector GetFocusLocation() const override;
	virtual bool CanInteract(const AShadeCharacter* Shade) const override;

	/** Landing spot relative to the actor. */
	UPROPERTY(EditAnywhere, Category = "Pot")
	FVector LandOffset = FVector(0.f, -160.f, -110.f);

	UPROPERTY(EditAnywhere, Category = "Pot")
	float LandYaw = 25.f;

	UPROPERTY(EditAnywhere, Category = "Pot")
	float FallTime = 0.62f;

	UPROPERTY(VisibleAnywhere, Category = "Pot")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "Pot")
	TObjectPtr<UStaticMeshComponent> Pot;

	UPROPERTY(VisibleAnywhere, Category = "Pot")
	TObjectPtr<UStaticMeshComponent> Plant;

	/** Earth filling the pot around the trunk. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pot")
	TObjectPtr<UStaticMeshComponent> Soil;

	/** Invisible box matching the dense part of the foliage: it blocks only the Sunlight channel. */
	UPROPERTY(VisibleAnywhere, Category = "Pot")
	TObjectPtr<UBoxComponent> CanopyShade;

	bool IsFalling() const { return State == 1 || State == 2; }

private:
	int32 State = 0; // 0 idle, 1 wobble, 2 falling, 3 landed
	float StateTime = 0.f;
	FShadeSpring Bounce;
};

/** A sleeping cat. Biscotto (the goal) has lost his shadow; the village's other nappers have theirs. */
UCLASS()
class SHADENSEEK_API AShadeNapCat : public AShadeInteractable
{
	GENERATED_BODY()

public:
	AShadeNapCat();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanInteract(const AShadeCharacter* Shade) const override;
	virtual void Interact(AShadeCharacter* Shade) override;
	virtual FVector GetFocusLocation() const override;

	/** True for Biscotto: interacting starts the reunion. */
	UPROPERTY(EditAnywhere, Category = "Cat")
	bool bIsBiscotto = false;

	UPROPERTY(EditAnywhere, Category = "Cat")
	FLinearColor CoatTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, Category = "Cat")
	float CoatDark = 0.f;

	UPROPERTY(EditAnywhere, Category = "Cat")
	float CatScale = 1.f;

	UPROPERTY(VisibleAnywhere, Category = "Cat")
	TObjectPtr<UPoseableMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "Cat")
	TObjectPtr<UShadeCatRig> Rig;

	void WakeUp();
	void SetHasShadow(bool bShadow);
	bool IsAwake() const { return bAwake; }
	UShadeCatRig* GetRig() const { return Rig; }
	UAudioComponent* GetPurr() const { return Purr; }
	void SetPurrVolume(float V) { PurrTarget = V; }

private:
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Purr;

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> EyeMID;

	bool bAwake = false;
	float WakeTime = 0.f;
	float PurrTarget = 0.f;
	float PurrLevel = 0.f;
};

/** Fires a story event when Shade enters it (once). */
UCLASS()
class SHADENSEEK_API AShadeZone : public AActor
{
	GENERATED_BODY()

public:
	AShadeZone();

	UPROPERTY(EditAnywhere, Category = "Shade")
	FName EventName;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UBoxComponent> Box;

protected:
	virtual void NotifyActorBeginOverlap(AActor* Other) override;
	bool bFired = false;
};

/** Plays random one-shots (gulls, swifts, distant voices of the harbour) around the listener. */
UCLASS()
class SHADENSEEK_API AShadeSoundScatter : public AActor
{
	GENERATED_BODY()

public:
	AShadeSoundScatter();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "Sound")
	FString SoundName = TEXT("SFX_Gull");

	UPROPERTY(EditAnywhere, Category = "Sound")
	int32 Variants = 3;

	UPROPERTY(EditAnywhere, Category = "Sound")
	FVector2D Interval = FVector2D(5.f, 12.f);

	UPROPERTY(EditAnywhere, Category = "Sound")
	FVector2D Distance = FVector2D(1500.f, 4000.f);

	UPROPERTY(EditAnywhere, Category = "Sound")
	float Height = 900.f;

	UPROPERTY(EditAnywhere, Category = "Sound")
	float Volume = 0.6f;

	/** Bias direction (world yaw, deg) for where sounds come from; < -360 = anywhere. */
	UPROPERTY(EditAnywhere, Category = "Sound")
	float BiasYaw = -1000.f;

private:
	float Timer = 3.f;
};
