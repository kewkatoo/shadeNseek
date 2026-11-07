#include "ShadeInteractables.h"
#include "shadeNseek.h"
#include "ShadeAssets.h"
#include "ShadeCharacter.h"
#include "ShadeCatRig.h"
#include "ShadeGameMode.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

#define LOCTEXT_NAMESPACE "ShadeInteractables"

namespace
{
	AShadeGameMode* GM(const UObject* Ctx)
	{
		const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
		return W ? W->GetAuthGameMode<AShadeGameMode>() : nullptr;
	}

	UStaticMeshComponent* MakeMesh(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetMobility(EComponentMobility::Movable);
		return C;
	}

	/** Visual-only mesh that still casts *gameplay* shade (blocks only the Sunlight channel). */
	void SunOnlyCollision(UPrimitiveComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		C->SetCollisionResponseToAllChannels(ECR_Ignore);
		C->SetCollisionResponseToChannel(ECC_Sunlight, ECR_Block);
	}

	void NoCollision(UPrimitiveComponent* C)
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	void DefaultMesh(UStaticMeshComponent* C, const TCHAR* Name)
	{
		if (C && !C->GetStaticMesh())
		{
			if (UStaticMesh* M = ShadeAssets::Mesh(Name))
			{
				C->SetStaticMesh(M);
			}
		}
	}
}

// =====================================================================================================================
// Base
// =====================================================================================================================

AShadeInteractable::AShadeInteractable()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	FocusPoint = CreateDefaultSubobject<USceneComponent>(TEXT("FocusPoint"));
	FocusPoint->SetupAttachment(Root);
	FocusPoint->SetRelativeLocation(FVector(0.f, 0.f, 60.f));
}

bool AShadeInteractable::CanInteract(const AShadeCharacter* Shade) const
{
	return !bSolved && Shade != nullptr;
}

FVector AShadeInteractable::GetFocusLocation() const
{
	return FocusPoint ? FocusPoint->GetComponentLocation() : GetActorLocation();
}

FVector AShadeInteractable::GetInteractLocation() const
{
	return GetActorLocation();
}

void AShadeInteractable::MarkSolved()
{
	if (bSolved)
	{
		return;
	}
	bSolved = true;
	UE_LOG(LogShade, Log, TEXT("SHADE_EVENT solved %s (%s)"), *GetName(), *SolvedEvent.ToString());
	if (AShadeGameMode* G = GM(this))
	{
		G->NotifyEvent(SolvedEvent, this);
	}
}

// =====================================================================================================================
// Lemon cart
// =====================================================================================================================

AShadeLemonCart::AShadeLemonCart()
{
	Verb = LOCTEXT("CartVerb", "Push");
	Label = LOCTEXT("CartLabel", "Lemon cart");
	InteractRange = 95.f;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetBoxExtent(FVector(105.f, 46.f, 45.f));
	Body->SetRelativeLocation(FVector(0.f, 0.f, 47.f));
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Body->SetCollisionResponseToChannel(ECC_Sunlight, ECR_Ignore);
	Body->SetMobility(EComponentMobility::Movable);

	CartMesh = MakeMesh(this, TEXT("CartMesh"), Root);
	SunOnlyCollision(CartMesh);
	WheelL = MakeMesh(this, TEXT("WheelL"), Root);
	WheelR = MakeMesh(this, TEXT("WheelR"), Root);
	WheelL->SetRelativeLocation(FVector(-10.f, -48.f, WheelRadius));
	WheelR->SetRelativeLocation(FVector(-10.f, 48.f, WheelRadius));
	NoCollision(WheelL);
	NoCollision(WheelR);
	FocusPoint->SetRelativeLocation(FVector(-90.f, 0.f, 95.f));
	Root->SetMobility(EComponentMobility::Movable);
}

void AShadeLemonCart::BeginPlay()
{
	Super::BeginPlay();
	DefaultMesh(CartMesh, TEXT("SM_LemonCart"));
	DefaultMesh(WheelL, TEXT("SM_CartWheel"));
	DefaultMesh(WheelR, TEXT("SM_CartWheel"));
	StartLocation = GetActorLocation();
	RollAudio = ShadeAssets::Loop(Root, TEXT("SFX_CartRoll_Loop"), 0.f, true);
}

bool AShadeLemonCart::CanInteract(const AShadeCharacter* Shade) const
{
	// The cart stays usable after it is "solved" so players can fine-tune it.
	return Shade != nullptr && Offset < TrackLength - 1.f;
}

FVector AShadeLemonCart::GetGrabLocation() const
{
	return GetActorLocation() - GetActorForwardVector() * GrabDistance;
}

FVector AShadeLemonCart::GetInteractLocation() const
{
	return GetGrabLocation();
}

FVector AShadeLemonCart::GetFocusLocation() const
{
	return GetActorLocation() - GetActorForwardVector() * (GrabDistance - 40.f) + FVector(0.f, 0.f, 105.f);
}

void AShadeLemonCart::Interact(AShadeCharacter* Shade)
{
	if (Shade)
	{
		Shade->ToggleGrab(this);
	}
}

float AShadeLemonCart::Push(float Delta)
{
	const float Target = FMath::Clamp(Offset + Delta, 0.f, TrackLength);
	const float Want = Target - Offset;
	if (FMath::Abs(Want) < 0.01f)
	{
		if (!bHitEnd && Delta > 0.f && Offset >= TrackLength - 0.5f)
		{
			bHitEnd = true;
			ShadeAssets::PlayAt(this, TEXT("SFX_CartBump"), GetActorLocation(), 0.8f);
		}
		return 0.f;
	}
	FHitResult Hit;
	const FVector Before = GetActorLocation();
	SetActorLocation(StartLocation + GetActorForwardVector() * Target, true, &Hit);
	const float Moved = FVector::DotProduct(GetActorLocation() - Before, GetActorForwardVector());
	Offset += Moved;
	if (Hit.bBlockingHit && FMath::Abs(Moved) < FMath::Abs(Want) * 0.5f && !bHitEnd)
	{
		bHitEnd = true;
		ShadeAssets::PlayAt(this, TEXT("SFX_CartBump"), GetActorLocation(), 0.8f);
	}
	if (FMath::Abs(Moved) > 0.5f)
	{
		bHitEnd = false;
	}
	if (Offset >= SolvedOffset)
	{
		MarkSolved();
	}
	return Moved;
}

void AShadeLemonCart::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Max(DeltaSeconds, 1e-4f);
	const float V = (Offset - LastOffset) / Dt;
	LastOffset = Offset;
	Velocity = ShadeEase::Approach(Velocity, V, 10.f, Dt);

	WheelAngle += (Velocity * Dt) / FMath::Max(WheelRadius, 1.f) * (180.f / PI);
	const FRotator WR(-WheelAngle, 0.f, 0.f);
	WheelL->SetRelativeRotation(WR);
	WheelR->SetRelativeRotation(WR);
	// The crates jiggle a little on the cobbles.
	const float Jig = FMath::Clamp(FMath::Abs(Velocity) / 120.f, 0.f, 1.f);
	CartMesh->SetRelativeRotation(FRotator(FMath::Sin(GetWorld()->TimeSeconds * 31.f) * 0.5f * Jig, 0.f, FMath::Sin(GetWorld()->TimeSeconds * 23.f) * 0.7f * Jig));

	if (RollAudio)
	{
		RollAudio->SetVolumeMultiplier(FMath::Clamp(FMath::Abs(Velocity) / 110.f, 0.f, 1.f) * 0.9f);
		RollAudio->SetPitchMultiplier(0.85f + 0.3f * Jig);
	}
	RattleTimer -= Dt;
	if (Jig > 0.25f && RattleTimer <= 0.f)
	{
		RattleTimer = FMath::FRandRange(0.35f, 0.8f);
		ShadeAssets::PlayVariantAt(this, TEXT("SFX_LemonRattle"), 3, GetActorLocation() + FVector(0, 0, 80), 0.45f * Jig);
	}
}

// =====================================================================================================================
// Awning
// =====================================================================================================================

AShadeAwning::AShadeAwning()
{
	Verb = LOCTEXT("AwningVerb", "Tug");
	Label = LOCTEXT("AwningLabel", "Awning cord");
	InteractRange = 90.f;

	Roller = MakeMesh(this, TEXT("Roller"), Root);
	NoCollision(Roller);
	Roller->SetCollisionResponseToChannel(ECC_Sunlight, ECR_Block);
	Canvas = MakeMesh(this, TEXT("Canvas"), Root);
	SunOnlyCollision(Canvas);
	Valance = MakeMesh(this, TEXT("Valance"), Root);
	SunOnlyCollision(Valance);
	ArmL = MakeMesh(this, TEXT("ArmL"), Root);
	ArmR = MakeMesh(this, TEXT("ArmR"), Root);
	NoCollision(ArmL);
	NoCollision(ArmR);
	Cord = MakeMesh(this, TEXT("Cord"), Root);
	Handle = MakeMesh(this, TEXT("Handle"), Root);
	NoCollision(Cord);
	NoCollision(Handle);
	Cord->SetCastShadow(false);
}

void AShadeAwning::BeginPlay()
{
	Super::BeginPlay();
	DefaultMesh(Roller, TEXT("SM_AwningRoller"));
	DefaultMesh(Canvas, TEXT("SM_AwningCanvas"));
	DefaultMesh(Valance, TEXT("SM_AwningValance"));
	DefaultMesh(ArmL, TEXT("SM_AwningArm"));
	DefaultMesh(ArmR, TEXT("SM_AwningArm"));
	DefaultMesh(Cord, TEXT("SM_Cord"));
	DefaultMesh(Handle, TEXT("SM_CordHandle"));
	Extension = StartExtension;
	ArmL->SetVisibility(bShowArms);
	ArmR->SetVisibility(bShowArms);
	UpdateGeometry();
}

FVector AShadeAwning::GetInteractLocation() const
{
	return GetActorTransform().TransformPosition(CordLocal + InteractOffset);
}

FVector AShadeAwning::GetFocusLocation() const
{
	return GetActorTransform().TransformPosition(CordLocal + FVector(0.f, 0.f, 55.f));
}

void AShadeAwning::Interact(AShadeCharacter* Shade)
{
	if (bSolved)
	{
		return;
	}
	bHolding = true;
	ShadeAssets::PlayAt(this, TEXT("SFX_AwningFlap"), GetActorLocation(), 0.6f, 1.1f);
}

void AShadeAwning::EndInteract(AShadeCharacter* Shade)
{
	bHolding = false;
}

void AShadeAwning::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	Time += Dt;
	if (bHolding && !bSolved)
	{
		const float Before = Extension;
		Extension = FMath::Min(1.f, Extension + OpenSpeed * Dt);
		TugPhase += Dt * 7.5f;
		ClickAccum += Extension - Before;
		if (ClickAccum > 0.07f)
		{
			ClickAccum = 0.f;
			ShadeAssets::PlayAt(this, TEXT("SFX_AwningClick"), GetActorTransform().TransformPosition(FVector(0, CordLocal.Y, 0)), 0.55f, FMath::FRandRange(0.94f, 1.08f));
		}
		Flutter.Velocity += Dt * 30.f;
		if (Extension >= 1.f)
		{
			bHolding = false;
			ShadeAssets::PlayAt(this, TEXT("SFX_AwningOpen"), GetActorLocation(), 0.9f);
			Flutter.Velocity += 8.f;
			MarkSolved();
		}
	}
	Flutter.Update(0.f, 60.f, 5.f, Dt);
	UpdateGeometry();
}

void AShadeAwning::UpdateGeometry()
{
	const float E = ShadeEase::Clamp01(Extension);
	const float Len = FMath::Max(Depth * E, 2.f);
	Roller->SetRelativeScale3D(FVector(1.f, Width / 100.f, 1.f));
	const float SlopeRad = FMath::DegreesToRadians(Slope);
	// Canvas mesh is authored 100 cm deep (local +X, sloping down) x 100 cm wide (local Y centred).
	Canvas->SetRelativeLocation(FVector(6.f, 0.f, -6.f));
	Canvas->SetRelativeRotation(FRotator(-Slope - Flutter.Value * 0.6f, 0.f, 0.f));
	Canvas->SetRelativeScale3D(FVector(Len / 100.f, Width / 100.f, 1.f));
	const FVector Front(6.f + Len * FMath::Cos(SlopeRad), 0.f, -6.f - Len * FMath::Sin(SlopeRad));
	Valance->SetRelativeLocation(Front);
	Valance->SetRelativeRotation(FRotator(0.f, 0.f, 0.f));
	Valance->SetRelativeScale3D(FVector(1.f, Width / 100.f, 1.f + 0.04f * FMath::Sin(Time * 3.f)));
	Valance->SetVisibility(E > 0.03f);

	// Folding arms: from a pivot on the wall below the roller to the front bar.
	for (int32 i = 0; i < 2; ++i)
	{
		UStaticMeshComponent* A = i == 0 ? ArmL : ArmR;
		const FVector Pivot(4.f, (i == 0 ? -1.f : 1.f) * (Width * 0.5f - 20.f), -70.f);
		const FVector Tip(Front.X, Pivot.Y, Front.Z);
		const FVector D = Tip - Pivot;
		A->SetRelativeLocation(Pivot);
		A->SetRelativeRotation(D.Rotation());
		A->SetRelativeScale3D(FVector(FMath::Max(D.Size(), 1.f) / 100.f, 1.f, 1.f));
	}

	// Cord hangs still from a cleat on the wall (its sway and tug bob read as jitter).
	const FVector Top(CordLocal.X, CordLocal.Y, -18.f);
	const FVector Bottom = CordLocal;
	Cord->SetRelativeLocation(Bottom);
	Cord->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	Cord->SetRelativeScale3D(FVector(FMath::Max((Top - Bottom).Size(), 1.f) / 100.f, 1.f, 1.f));
	Handle->SetRelativeLocation(Bottom);
	Handle->SetRelativeRotation(FRotator::ZeroRotator);
}

// =====================================================================================================================
// Flower pot
// =====================================================================================================================

AShadeFlowerPot::AShadeFlowerPot()
{
	Verb = LOCTEXT("PotVerb", "Nudge");
	Label = LOCTEXT("PotLabel", "Lemon tree pot");
	InteractRange = 95.f;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);
	Pivot->SetMobility(EComponentMobility::Movable);
	Pot = MakeMesh(this, TEXT("Pot"), Pivot);
	Pot->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Plant = MakeMesh(this, TEXT("Plant"), Pivot);
	NoCollision(Plant);
	Soil = MakeMesh(this, TEXT("Soil"), Pivot);
	NoCollision(Soil);
	CanopyShade = CreateDefaultSubobject<UBoxComponent>(TEXT("CanopyShade"));
	CanopyShade->SetupAttachment(Pivot);
	CanopyShade->SetBoxExtent(FVector(48.f, 48.f, 38.f));
	CanopyShade->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	SunOnlyCollision(CanopyShade);
	CanopyShade->SetMobility(EComponentMobility::Movable);
	FocusPoint->SetRelativeLocation(FVector(0.f, 0.f, 75.f));
	Root->SetMobility(EComponentMobility::Movable);
}

void AShadeFlowerPot::BeginPlay()
{
	Super::BeginPlay();
	if (!Pot->GetStaticMesh())
	{
		Pot->SetStaticMesh(ShadeAssets::PropMesh(TEXT("planter_pot_clay"), TEXT("SM_planter_pot_clay")));
	}
	if (const UStaticMesh* M = Pot->GetStaticMesh())
	{
		// The foot of the pot is narrower than its widest extent.
		RimRadius = M->GetBounds().BoxExtent.X * Pot->GetRelativeScale3D().X * 0.72f;
	}
	const FVector Flat(LandOffset.X, LandOffset.Y, 0.f);
	ThrowDir = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat.GetSafeNormal();
	PlantRest = Plant->GetRelativeLocation();
}

void AShadeFlowerPot::PosePot(const FVector& Base, float Angle, float YawDeg)
{
	const FVector Axis = FVector::CrossProduct(FVector::UpVector, ThrowDir).GetSafeNormal();
	const FQuat TiltQ(Axis, FMath::DegreesToRadians(Angle));
	const FVector Rim = ThrowDir * (Angle >= 0.f ? RimRadius : -RimRadius);
	Pivot->SetRelativeLocation(Base + Rim - TiltQ.RotateVector(Rim));
	Pivot->SetRelativeRotation(TiltQ * FQuat(FVector::UpVector, FMath::DegreesToRadians(YawDeg)));
	Tilt = Angle;
}

bool AShadeFlowerPot::CanInteract(const AShadeCharacter* Shade) const
{
	// Only from up on the wall (the wall's own shadow at its foot must not let Shade nudge it from the ground).
	return State == 0 && Shade != nullptr && Shade->GetActorLocation().Z > GetActorLocation().Z - 25.f;
}

FVector AShadeFlowerPot::GetFocusLocation() const
{
	return Pivot->GetComponentLocation() + FVector(0.f, 0.f, 80.f);
}

void AShadeFlowerPot::Interact(AShadeCharacter* Shade)
{
	if (State != 0)
	{
		return;
	}
	State = 1;
	StateTime = 0.f;
	ShadeAssets::PlayAt(this, TEXT("SFX_PotWobble"), GetActorLocation(), 0.8f);
}

void AShadeFlowerPot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	StateTime += Dt;
	const FVector Dir2 = ThrowDir;
	const float TeeterTime = 0.6f;
	const float Slide = 10.f;
	if (State == 1)
	{
		// Teeter: it rocks on its rim, front and back, while it shuffles to the edge; then it leans past the
		// balance point and goes (ending at 26 deg, turning at 130 deg/s).
		const float T = StateTime;
		const float Rock = 5.f * FMath::Sin(T * 2.f * PI / 0.2f) * ShadeEase::Smooth(T / 0.06f) * (1.f - ShadeEase::Smooth(T / 0.5f));
		const float Tip = 26.f * ShadeEase::InCubic(T / TeeterTime);
		PosePot(Dir2 * Slide * ShadeEase::Smooth(T / 0.45f), Rock + Tip, 0.f);
		if (StateTime >= TeeterTime)
		{
			State = 2;
			StateTime = 0.f;
			Pot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			ShadeAssets::PlayAt(this, TEXT("SFX_PotFall"), GetActorLocation(), 0.7f);
		}
	}
	else if (State == 2)
	{
		const float T = ShadeEase::Clamp01(StateTime / FallTime);
		// Rolls off the edge: steady drift outward, gravity downward (no vertical speed at the start).
		FVector Base = FMath::Lerp(Dir2 * Slide, FVector(LandOffset.X, LandOffset.Y, 0.f), T);
		Base.Z = LandOffset.Z * T * T;
		// Tumble: carries on tipping (to ~38 deg), then the heavy foot swings back under so it lands on its front
		// rim at 14 deg. Cubic Hermite from (26 deg, 130 deg/s) to (14 deg, -65 deg/s).
		const float T2 = T * T;
		const float T3 = T2 * T;
		const float Angle = (2.f * T3 - 3.f * T2 + 1.f) * 26.f + (T3 - 2.f * T2 + T) * 130.f * FallTime
			+ (-2.f * T3 + 3.f * T2) * 14.f + (T3 - T2) * -40.f;
		PosePot(Base, Angle, LandYaw * ShadeEase::Smooth(T));
		if (T >= 1.f)
		{
			State = 3;
			StateTime = 0.f;
			Bounce.Velocity = -3.2f;
			// Slaps down onto its foot, rocks back past upright and settles; the tree whips forward.
			Settle.Value = Tilt;
			Settle.Velocity = -90.f;
			Sway.Velocity += 70.f;
			Pot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			ShadeAssets::PlayAt(this, TEXT("SFX_PotLand"), Pivot->GetComponentLocation(), 1.f);
			if (AShadeGameMode* G = GM(this))
			{
				G->AddTrauma(0.35f);
				G->SpawnDust(Pivot->GetComponentLocation() + FVector(0, 0, 8.f), 1.f);
			}
			MarkSolved();
		}
	}
	else if (State == 3)
	{
		const float B = Bounce.Update(0.f, 220.f, 14.f, Dt);
		const float A = Settle.Update(3.f, 170.f, 7.f, Dt);
		PosePot(FVector(LandOffset.X, LandOffset.Y, LandOffset.Z), A, LandYaw);
		Pivot->SetRelativeScale3D(FVector(1.f - B * 0.04f, 1.f - B * 0.04f, 1.f + B * 0.08f));
	}

	// The tree has inertia: it lags the pot's swings and springs back, bending at the trunk foot (on the pot's axis).
	if (State != 0 && Dt > 1e-4f)
	{
		const float Rate = (Tilt - PrevTilt) / Dt;
		Sway.Velocity -= (Rate - PrevTiltRate) * 0.7f;
		PrevTilt = Tilt;
		PrevTiltRate = Rate;
		const float S = FMath::Clamp(Sway.Update(0.f, 110.f, 5.5f, Dt), -16.f, 16.f);
		// Plant is a child of the pivot: express the tilt axis in the pivot's (yawed) frame.
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, Dir2).GetSafeNormal();
		const FVector LocalAxis = Pivot->GetRelativeRotation().Quaternion().Inverse().RotateVector(Axis);
		const FQuat SwayQ(LocalAxis.GetSafeNormal(), FMath::DegreesToRadians(S));
		const FVector Foot(0.f, 0.f, PlantRest.Z);
		Plant->SetRelativeLocation(Foot + SwayQ.RotateVector(PlantRest - Foot));
		Plant->SetRelativeRotation(SwayQ);
	}
}

// =====================================================================================================================
// Washing line (moving shade)
// =====================================================================================================================

namespace
{
	const TCHAR* const GSheetMeshes[3] = { TEXT("SM_Sheet_White"), TEXT("SM_Sheet_Blue"), TEXT("SM_Sheet_Lemon") };
}

AShadeWashingLine::AShadeWashingLine()
{
	Verb = LOCTEXT("WashVerb", "Pull");
	Label = LOCTEXT("WashLabel", "Washing line");
	InteractRange = 95.f;
	Root->SetMobility(EComponentMobility::Movable);

	Line = MakeMesh(this, TEXT("Line"), Root);
	NoCollision(Line);
	Cord = MakeMesh(this, TEXT("Cord"), Root);
	Handle = MakeMesh(this, TEXT("Handle"), Root);
	NoCollision(Cord);
	NoCollision(Handle);
	Cord->SetCastShadow(false);
	for (int32 i = 0; i < 3; ++i)
	{
		USceneComponent* P = CreateDefaultSubobject<USceneComponent>(*FString::Printf(TEXT("SheetPivot%d"), i));
		P->SetupAttachment(Root);
		P->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* M = MakeMesh(this, *FString::Printf(TEXT("Sheet%d"), i), P);
		NoCollision(M);
		UBoxComponent* B = CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("SheetShade%d"), i));
		B->SetupAttachment(P);
		B->SetMobility(EComponentMobility::Movable);
		SunOnlyCollision(B);
		SheetPivots.Add(P);
		Sheets.Add(M);
		SheetShade.Add(B);
	}
}

void AShadeWashingLine::BeginPlay()
{
	Super::BeginPlay();
	DefaultMesh(Line, TEXT("SM_Rope_100"));
	DefaultMesh(Cord, TEXT("SM_Cord"));
	DefaultMesh(Handle, TEXT("SM_CordHandle"));
	Swing.SetNum(Sheets.Num());
	for (int32 i = 0; i < Sheets.Num(); ++i)
	{
		DefaultMesh(Sheets[i], GSheetMeshes[i % 3]);
		SheetShade[i]->SetBoxExtent(FVector(SheetWidth * 0.5f, 1.5f, SheetDrop * 0.5f));
		SheetShade[i]->SetRelativeLocation(FVector(0.f, 0.f, -SheetDrop * 0.5f));
	}
	// The line climbs with the stairs from the pulley on the corner to a hook on the next house.
	const FVector LineVec(Span, 0.f, Rise);
	Line->SetRelativeLocation(FVector::ZeroVector);
	Line->SetRelativeRotation(LineVec.Rotation());
	Line->SetRelativeScale3D(FVector(LineVec.Size() / 100.f, 1.2f, 1.2f));
	// The cord runs down the wall from the pulley to a handle at cat height.
	const FVector Top(CordLocal.X, CordLocal.Y, -12.f);
	Cord->SetRelativeLocation(CordLocal);
	Cord->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	Cord->SetRelativeScale3D(FVector(FMath::Max((Top - CordLocal).Size(), 1.f) / 100.f, 1.f, 1.f));
	Handle->SetRelativeLocation(CordLocal);
	FlapAudio = ShadeAssets::Loop(Root, TEXT("SFX_LaundryFlap_Loop"), 0.f, true);
	if (FlapAudio)
	{
		FlapAudio->SetRelativeLocation(FVector(Span * 0.5f, 0.f, Rise * 0.5f - 60.f));
	}
	Extension = 0.f;
	UpdateSheets(0.f);
}

FVector AShadeWashingLine::GetInteractLocation() const
{
	return GetActorTransform().TransformPosition(CordLocal + InteractOffset);
}

FVector AShadeWashingLine::GetFocusLocation() const
{
	return GetActorTransform().TransformPosition(CordLocal + FVector(0.f, 0.f, 55.f));
}

void AShadeWashingLine::Interact(AShadeCharacter* Shade)
{
	if (bSolved)
	{
		return;
	}
	bHolding = true;
	ShadeAssets::PlayAt(this, TEXT("SFX_RopeCreak"), GetActorTransform().TransformPosition(CordLocal), 0.7f);
}

void AShadeWashingLine::EndInteract(AShadeCharacter* Shade)
{
	bHolding = false;
}

float AShadeWashingLine::GustEnvelope(float T) const
{
	// Deterministic sea breeze: a gust of GustTime (eased in and out), then a lull. The rhythm is the puzzle.
	const float P = FMath::Max(GustPeriod, 1.f);
	const float X = FMath::Fmod(FMath::Fmod(T, P) + P, P);
	if (X > GustTime)
	{
		return 0.f;
	}
	return ShadeEase::Smooth(X / 0.3f) * (1.f - ShadeEase::Smooth((X - (GustTime - 0.45f)) / 0.45f));
}

bool AShadeWashingLine::IsGusting() const
{
	return GustEnvelope(GustClock) > 0.25f;
}

void AShadeWashingLine::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	Time += Dt;
	GustClock += Dt;
	if (bHolding && !bSolved)
	{
		const float Before = Extension;
		Extension = FMath::Min(1.f, Extension + OpenSpeed * Dt);
		ClickAccum += Extension - Before;
		if (ClickAccum > 0.1f)
		{
			ClickAccum = 0.f;
			ShadeAssets::PlayAt(this, TEXT("SFX_AwningClick"), GetActorTransform().TransformPosition(FVector(0.f, 0.f, -20.f)), 0.5f, FMath::FRandRange(0.9f, 1.05f));
		}
		if (Extension >= 1.f)
		{
			bHolding = false;
			MarkSolved();
		}
	}
	const bool bGust = IsGusting();
	if (bGust && !bWasGusting && Extension > 0.3f)
	{
		ShadeAssets::PlayAt(this, TEXT("SFX_AwningFlap"), GetActorTransform().TransformPosition(FVector(Span * 0.5f, 0.f, 0.f)), 0.75f, FMath::FRandRange(0.95f, 1.1f));
	}
	bWasGusting = bGust;
	if (FlapAudio)
	{
		FlapAudio->SetVolumeMultiplier(ShadeEase::Clamp01(Extension) * (0.25f + 0.75f * GustEnvelope(GustClock)));
	}
	UpdateSheets(Dt);
}

void AShadeWashingLine::UpdateSheets(float Dt)
{
	const int32 N = Sheets.Num();
	for (int32 i = 0; i < N; ++i)
	{
		// Pegged on the running line: the lead sheet goes out first, the others follow; retracted they bunch up by the
		// pulley, gathered narrow.
		const float OpenX = (i + 0.5f) * Span / N;
		const float BunchX = 10.f + i * 9.f;
		const float X = FMath::Max(BunchX, OpenX - (1.f - ShadeEase::Clamp01(Extension)) * Span);
		const float Out = ShadeEase::Clamp01((X - BunchX) / FMath::Max(OpenX - BunchX, 1.f));
		SheetPivots[i]->SetRelativeLocation(FVector(X, 0.f, X * Rise / FMath::Max(Span, 1.f)));

		// Lift inland (-Y): the gust (each sheet a beat behind the one nearer the sea gap's mouth), a pre-gust billow
		// that tells the player a gust is coming, and a calm flutter.
		const float Lag = 0.07f * i;
		const float Gust = GustEnvelope(GustClock - Lag);
		const float Ahead = GustEnvelope(GustClock - Lag + 0.5f);
		const float Calm = 5.f * FMath::Sin(Time * 2.1f + i * 1.7f) + 2.f * FMath::Sin(Time * 5.3f + i);
		const float Billow = 10.f * Ahead * (1.f - Gust) * (0.6f + 0.4f * FMath::Sin(Time * 9.f + i));
		const float Target = (GustLift * Gust + Calm + Billow) * Out;
		if (Dt > 0.f && Swing.IsValidIndex(i))
		{
			Swing[i].Update(Target, 70.f, 9.f, Dt);
		}
		const float Lift = Swing.IsValidIndex(i) ? Swing[i].Value : 0.f;
		SheetPivots[i]->SetRelativeRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(-Lift)));

		if (const UStaticMesh* M = Sheets[i]->GetStaticMesh())
		{
			const FVector E = M->GetBounds().BoxExtent;
			const float W = FMath::Max(E.X * 2.f, 1.f), H = FMath::Max(E.Z * 2.f, 1.f);
			Sheets[i]->SetRelativeScale3D(FVector(SheetWidth / W * FMath::Lerp(0.3f, 1.f, Out), 1.f, SheetDrop / H));
		}
		SheetShade[i]->SetRelativeScale3D(FVector(FMath::Lerp(0.3f, 1.f, Out), 1.f, 1.f));
	}
}

// =====================================================================================================================
// Shutter (timed shade)
// =====================================================================================================================

AShadeShutter::AShadeShutter()
{
	Verb = LOCTEXT("ShutterVerb", "Tug");
	Label = LOCTEXT("ShutterLabel", "Shutter cord");
	InteractRange = 95.f;
	Root->SetMobility(EComponentMobility::Movable);
	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Root);
	Hinge->SetMobility(EComponentMobility::Movable);
	Panel = MakeMesh(this, TEXT("Panel"), Hinge);
	NoCollision(Panel);
	PanelShade = CreateDefaultSubobject<UBoxComponent>(TEXT("PanelShade"));
	PanelShade->SetupAttachment(Hinge);
	PanelShade->SetMobility(EComponentMobility::Movable);
	SunOnlyCollision(PanelShade);
	Cord = MakeMesh(this, TEXT("Cord"), Root);
	Handle = MakeMesh(this, TEXT("Handle"), Root);
	NoCollision(Cord);
	NoCollision(Handle);
	Cord->SetCastShadow(false);
}

void AShadeShutter::BeginPlay()
{
	Super::BeginPlay();
	DefaultMesh(Panel, TEXT("SM_BigShutter"));
	DefaultMesh(Cord, TEXT("SM_Cord"));
	DefaultMesh(Handle, TEXT("SM_CordHandle"));
	PanelShade->SetBoxExtent(FVector(2.5f, PanelWidth * 0.5f, PanelHeight * 0.5f));
	PanelShade->SetRelativeLocation(FVector(3.f, PanelWidth * 0.5f, PanelHeight * 0.5f));
	const FVector Top(CordLocal.X, CordLocal.Y, PanelHeight * 0.5f);
	Cord->SetRelativeLocation(CordLocal);
	Cord->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	Cord->SetRelativeScale3D(FVector(FMath::Max((Top - CordLocal).Size(), 1.f) / 100.f, 1.f, 1.f));
	Handle->SetRelativeLocation(CordLocal);
	Hinge->SetRelativeRotation(FRotator::ZeroRotator);
}

bool AShadeShutter::CanInteract(const AShadeCharacter* Shade) const
{
	return Shade != nullptr && !bSolved && State == 0;
}

FVector AShadeShutter::GetInteractLocation() const
{
	return GetActorTransform().TransformPosition(CordLocal + InteractOffset);
}

FVector AShadeShutter::GetFocusLocation() const
{
	return GetActorTransform().TransformPosition(CordLocal + FVector(0.f, 0.f, 55.f));
}

void AShadeShutter::Interact(AShadeCharacter* Shade)
{
	if (State != 0 || bSolved)
	{
		return;
	}
	State = 1;
	StateTime = 0.f;
	UE_LOG(LogShade, Log, TEXT("SHADE_SHUTTER tug: opening (panel shade box %s)"), *PanelShade->Bounds.GetBox().ToString());
	ShadeAssets::PlayAt(this, TEXT("SFX_AwningClick"), GetActorTransform().TransformPosition(CordLocal), 0.6f, 0.9f);
	ShadeAssets::PlayAt(this, TEXT("SFX_ShutterCreak"), Hinge->GetComponentLocation() + FVector(0, 0, PanelHeight * 0.5f), 0.8f, 1.15f);
}

void AShadeShutter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	StateTime += Dt;
	if (!bSolved && !DoneEvent.IsNone() && State == 0)
	{
		if (const AShadeGameMode* G = GM(this))
		{
			bSolved = G->HasEvent(DoneEvent);
		}
	}
	const FVector Mid = Hinge->GetComponentLocation() + FVector(0, 0, PanelHeight * 0.5f);
	switch (State)
	{
	case 1: // flung open by the tug, overshoots onto its stop
		Angle = OpenAngle * ShadeEase::OutBack(StateTime / FMath::Max(OpenTime, 0.05f), 1.1f);
		if (StateTime >= OpenTime)
		{
			State = 2;
			StateTime = 0.f;
			Angle = OpenAngle;
			Bang.Velocity = -40.f;
			UE_LOG(LogShade, Log, TEXT("SHADE_SHUTTER open (panel shade box %s)"), *PanelShade->Bounds.GetBox().ToString());
			ShadeAssets::PlayAt(this, TEXT("SFX_Release"), Mid, 0.6f, 0.85f);
		}
		break;
	case 2: // the breeze holds it a moment
		Angle = OpenAngle;
		if (StateTime >= HoldOpen)
		{
			State = 3;
			StateTime = 0.f;
			ShadeAssets::PlayAt(this, TEXT("SFX_ShutterCreak"), Mid, 0.8f, 0.95f);
		}
		break;
	case 3: // and creaks it shut, slowly at first
	{
		const float U = ShadeEase::Clamp01(StateTime / FMath::Max(CloseTime, 0.05f));
		Angle = OpenAngle * (1.f - U * U);
		if (U >= 1.f)
		{
			State = 0;
			StateTime = 0.f;
			Angle = 0.f;
			Bang.Velocity = 60.f;
			ShadeAssets::PlayAt(this, TEXT("SFX_CartBump"), Mid, 0.55f, 1.1f);
		}
		break;
	}
	default:
		break;
	}
	Bang.Update(0.f, 260.f, 14.f, Dt);
	const float A = FMath::Max(0.f, Angle + (State == 1 ? 0.f : Bang.Value));
	Hinge->SetRelativeRotation(FRotator(0.f, -A, 0.f));
}

// =====================================================================================================================
// Nap cat / Biscotto
// =====================================================================================================================

AShadeNapCat::AShadeNapCat()
{
	Verb = LOCTEXT("CatVerb", "Wake");
	Label = LOCTEXT("CatLabel", "Biscotto");
	InteractRange = 120.f;

	Mesh = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Root);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Rig = CreateDefaultSubobject<UShadeCatRig>(TEXT("Rig"));
	FocusPoint->SetRelativeLocation(FVector(0.f, 0.f, 45.f));
}

void AShadeNapCat::BeginPlay()
{
	Super::BeginPlay();
	if (!Mesh->GetSkinnedAsset())
	{
		Mesh->SetSkinnedAssetAndUpdate(ShadeAssets::Skeletal(TEXT("SK_Cat")));
	}
	for (UMaterialInstanceDynamic* MID : { ShadeAssets::SetCatMaterial(Mesh, TEXT("M_CatBody"), 0, TEXT("MI_CatFur")),
										   ShadeAssets::SetCatMaterial(Mesh, TEXT("M_CatShell"), 3, TEXT("MI_CatShell")) })
	{
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("CoatTint"), CoatTint);
			MID->SetScalarParameterValue(TEXT("CoatDark"), CoatDark);
		}
	}
	EyeMID = ShadeAssets::SetCatMaterial(Mesh, TEXT("M_CatEye"), 1, TEXT("MI_CatEye"));
	ShadeAssets::SetCatMaterial(Mesh, TEXT("M_CatWhisker"), 2, TEXT("MI_CatWhisker"));
	if (EyeMID)
	{
		EyeMID->SetVectorParameterValue(TEXT("LidColor"), CoatTint * FLinearColor(0.6f, 0.26f, 0.075f) * (1.f - 0.82f * CoatDark));
	}
	Rig->Init(Mesh);
	Mesh->SetRelativeRotation(FRotator(0.f, Rig->GetFacingYaw(), 0.f));
	Mesh->SetRelativeScale3D(FVector(CatScale * Rig->GetUnitScale()));
	Rig->MeshScale = CatScale;
	Rig->SetPose(ECatPose::Sleep, 0.01f);
	Rig->Excitement = 0.2f;
	SetHasShadow(!bIsBiscotto);
	Purr = ShadeAssets::Loop(Root, TEXT("SFX_Purr_Loop"), 0.f, true);
	PurrTarget = bIsBiscotto ? 0.35f : 0.12f;
	if (!bIsBiscotto)
	{
		Verb = FText::GetEmpty();
	}
}

void AShadeNapCat::SetHasShadow(bool bShadow)
{
	Mesh->SetCastShadow(bShadow);
}

bool AShadeNapCat::CanInteract(const AShadeCharacter* Shade) const
{
	if (!bIsBiscotto || bAwake || !Shade)
	{
		return false;
	}
	const AShadeGameMode* G = GM(this);
	return !G || G->CanReunite();
}

FVector AShadeNapCat::GetFocusLocation() const
{
	return GetActorLocation() + FVector(0.f, 0.f, 45.f * CatScale);
}

void AShadeNapCat::Interact(AShadeCharacter* Shade)
{
	if (AShadeGameMode* G = GM(this))
	{
		G->BeginReunion(this);
	}
}

void AShadeNapCat::WakeUp()
{
	if (bAwake)
	{
		return;
	}
	bAwake = true;
	WakeTime = 0.f;
	Rig->Yawn();
	ShadeAssets::PlayAt(this, TEXT("SFX_Mrrp"), GetActorLocation() + FVector(0, 0, 30), 0.9f);
}

void AShadeNapCat::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	if (bAwake)
	{
		WakeTime += Dt;
		if (WakeTime > 1.6f && Rig->GetPose() == ECatPose::Sleep)
		{
			Rig->SetPose(ECatPose::Sit, 1.1f);
			Rig->Excitement = 0.8f;
		}
	}
	PurrLevel = ShadeEase::Approach(PurrLevel, PurrTarget, 2.f, Dt);
	if (EyeMID)
	{
		EyeMID->SetScalarParameterValue(TEXT("Lid"), Rig->GetEyeLid());
	}
	if (Purr)
	{
		// Purr swells with each breath.
		Purr->SetVolumeMultiplier(PurrLevel * (0.8f + 0.2f * Rig->GetBreath()));
	}
}

// =====================================================================================================================
// Zone + sound scatter
// =====================================================================================================================

AShadeZone::AShadeZone()
{
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetBoxExtent(FVector(150.f, 150.f, 150.f));
	Box->SetCollisionProfileName(TEXT("Trigger"));
	Box->SetCollisionResponseToChannel(ECC_Sunlight, ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

void AShadeZone::NotifyActorBeginOverlap(AActor* Other)
{
	Super::NotifyActorBeginOverlap(Other);
	if (bFired || !Cast<AShadeCharacter>(Other))
	{
		return;
	}
	bFired = true;
	UE_LOG(LogShade, Log, TEXT("SHADE_EVENT zone %s"), *EventName.ToString());
	if (AShadeGameMode* G = GM(this))
	{
		G->NotifyEvent(EventName, this);
	}
}

AShadeSoundScatter::AShadeSoundScatter()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AShadeSoundScatter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Timer -= DeltaSeconds;
	if (Timer > 0.f)
	{
		return;
	}
	Timer = FMath::FRandRange(Interval.X, Interval.Y);
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector Center = Cam ? Cam->GetCameraLocation() : GetActorLocation();
	const float Yaw = BiasYaw > -360.f ? BiasYaw + FMath::FRandRange(-70.f, 70.f) : FMath::FRandRange(0.f, 360.f);
	const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector P = Center + Dir * FMath::FRandRange(Distance.X, Distance.Y) + FVector(0.f, 0.f, Height * FMath::FRandRange(0.6f, 1.2f));
	ShadeAssets::PlayVariantAt(this, *SoundName, Variants, P, Volume, 0.08f);
}

#undef LOCTEXT_NAMESPACE
