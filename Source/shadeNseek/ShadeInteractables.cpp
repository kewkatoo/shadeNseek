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
	UpdateGeometry();
}

FVector AShadeAwning::GetInteractLocation() const
{
	return GetActorTransform().TransformPosition(FVector(CordLocal.X + 30.f, CordLocal.Y, CordLocal.Z));
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

	// Cord hangs from a cleat on the wall; while tugging, the handle bobs.
	const float Tug = bHolding ? FMath::Max(0.f, FMath::Sin(TugPhase)) * 9.f : 0.f;
	const FVector Top(CordLocal.X, CordLocal.Y, -18.f);
	const FVector Bottom = CordLocal + FVector(0.f, 0.f, -Tug);
	Cord->SetRelativeLocation(Bottom);
	Cord->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	Cord->SetRelativeScale3D(FVector(FMath::Max((Top - Bottom).Size(), 1.f) / 100.f, 1.f, 1.f));
	Handle->SetRelativeLocation(Bottom);
	Handle->SetRelativeRotation(FRotator(0.f, 0.f, FMath::Sin(Time * 1.7f) * 4.f + Tug));
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
}

bool AShadeFlowerPot::CanInteract(const AShadeCharacter* Shade) const
{
	return State == 0 && Shade != nullptr;
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
	const FVector Dir2 = FVector(LandOffset.X, LandOffset.Y, 0.f).GetSafeNormal();
	// Tumble axis: perpendicular to the throw, in actor space.
	if (State == 1)
	{
		// Wobble: it rocks twice, each time a little further, then goes.
		const float W = FMath::Sin(StateTime * 26.f) * 5.f * ShadeEase::Window(StateTime, 0.f, 0.35f);
		Pivot->SetRelativeRotation(FRotator(W * Dir2.X, 0.f, -W * Dir2.Y));
		Pivot->SetRelativeLocation(Dir2 * 8.f * ShadeEase::Smooth(StateTime / 0.45f));
		if (StateTime > 0.45f)
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
		const FVector Start = Dir2 * 8.f;
		const FVector Lateral = FMath::Lerp(Start, FVector(LandOffset.X, LandOffset.Y, 0.f), T);
		// Ballistic: small hop up off the ledge then gravity.
		const float Z = 14.f * FMath::Sin(T * PI * 0.5f) * (1.f - T) + LandOffset.Z * T * T;
		Pivot->SetRelativeLocation(FVector(Lateral.X, Lateral.Y, Z));
		const float Tilt = FMath::Sin(T * PI) * 38.f;
		Pivot->SetRelativeRotation(FRotator(Tilt * Dir2.X, LandYaw * T, -Tilt * Dir2.Y));
		if (T >= 1.f)
		{
			State = 3;
			StateTime = 0.f;
			Bounce.Velocity = -3.2f;
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
		Pivot->SetRelativeScale3D(FVector(1.f - B * 0.04f, 1.f - B * 0.04f, 1.f + B * 0.08f));
		Pivot->SetRelativeRotation(FRotator(4.f * Dir2.X * (1.f + B), LandYaw, -4.f * Dir2.Y * (1.f + B)));
	}
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
	Mesh->SetMaterial(0, ShadeAssets::Material(TEXT("MI_CatFur")));
	Mesh->SetMaterial(1, ShadeAssets::Material(TEXT("MI_CatEye")));
	if (UMaterialInstanceDynamic* MID = ShadeAssets::MakeMID(Mesh, 0))
	{
		MID->SetVectorParameterValue(TEXT("CoatTint"), CoatTint);
		MID->SetScalarParameterValue(TEXT("CoatDark"), CoatDark);
	}
	Rig->Init(Mesh);
	Mesh->SetRelativeRotation(FRotator(0.f, Rig->GetFacingYaw(), 0.f));
	Mesh->SetRelativeScale3D(FVector(CatScale));
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
