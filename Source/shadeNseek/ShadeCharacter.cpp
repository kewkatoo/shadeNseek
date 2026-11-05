#include "ShadeCharacter.h"
#include "shadeNseek.h"
#include "ShadeAssets.h"
#include "ShadeCatRig.h"
#include "ShadeGameMode.h"
#include "ShadeInteractables.h"
#include "ShadeSunSubsystem.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	constexpr int32 GWispCount = 30;
	constexpr float GCapsuleRadius = 19.f;
	constexpr float GCapsuleHalf = 19.f;
	constexpr float GMeshScale = 1.12f;
}

AShadeCharacter::AShadeCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	UCapsuleComponent* Cap = GetCapsuleComponent();
	Cap->InitCapsuleSize(GCapsuleRadius, GCapsuleHalf);
	// Shade *is* a shadow: it never blocks sunlight (and never casts shade on itself).
	Cap->SetCollisionResponseToChannel(ECC_Sunlight, ECR_Ignore);
	Cap->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 620.f, 0.f);
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = 1700.f;
	Move->BrakingDecelerationWalking = 1500.f;
	Move->GroundFriction = 9.f;
	Move->JumpZVelocity = 560.f;
	Move->AirControl = 0.65f;
	Move->MaxStepHeight = 26.f;
	Move->SetWalkableFloorAngle(52.f);
	Move->bCanWalkOffLedgesWhenCrouching = true;
	Move->PerchRadiusThreshold = 6.f;

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 285.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 42.f);
	CameraBoom->TargetOffset = FVector(0.f, 0.f, 18.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 9.f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 14.f;
	CameraBoom->ProbeSize = 9.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->SetFieldOfView(BaseFOV);

	Body = CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootComponent);
	Body->SetRelativeLocation(FVector(0.f, 0.f, -GCapsuleHalf));
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetCastShadow(false);
	Body->bRenderCustomDepth = true;

	Rig = CreateDefaultSubobject<UShadeCatRig>(TEXT("Rig"));

	EyeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EyeLight"));
	EyeLight->SetupAttachment(Body);
	EyeLight->SetIntensityUnits(ELightUnits::Lumens);
	EyeLight->SetIntensity(18.f);
	EyeLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.2f));
	EyeLight->SetAttenuationRadius(70.f);
	EyeLight->SetCastShadows(false);
	EyeLight->SetSourceRadius(2.f);
}

AShadeGameMode* AShadeCharacter::GetShadeGameMode() const
{
	return GetWorld() ? GetWorld()->GetAuthGameMode<AShadeGameMode>() : nullptr;
}

void AShadeCharacter::BeginPlay()
{
	Super::BeginPlay();
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	if (USkeletalMesh* Cat = ShadeAssets::Skeletal(TEXT("SK_Cat")))
	{
		Body->SetSkinnedAssetAndUpdate(Cat);
	}
	Body->SetMaterial(0, ShadeAssets::Material(TEXT("M_ShadeBody")));
	Body->SetMaterial(1, ShadeAssets::Material(TEXT("M_ShadeEye")));
	BodyMID = ShadeAssets::MakeMID(Body, 0);
	EyeMID = ShadeAssets::MakeMID(Body, 1);
	Rig->Init(Body);
	Rig->MeshScale = GMeshScale;
	Rig->Excitement = 0.55f;
	Body->SetRelativeRotation(FRotator(0.f, Rig->GetFacingYaw(), 0.f));
	Body->SetRelativeScale3D(FVector(GMeshScale));
	if (Rig->IsReady())
	{
		// Eye glow rides on the head bone.
		EyeLight->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	}
	Rig->OnFootPlant = [this](int32 Leg)
	{
		const float Sp = GetVelocity().Size2D();
		ShadeAssets::PlayVariantAt(this, TEXT("SFX_Paw"), 6, GetActorLocation() - FVector(0, 0, GCapsuleHalf), FMath::Clamp(Sp / 500.f, 0.12f, 0.5f), 0.1f);
	};

	UStaticMesh* Quad = ShadeAssets::Mesh(TEXT("SM_WispQuad"));
	UMaterialInterface* WispMat = ShadeAssets::Material(TEXT("M_Wisp"));
	Wisps.SetNum(GWispCount);
	for (int32 i = 0; i < GWispCount; ++i)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, *FString::Printf(TEXT("Wisp%02d"), i));
		C->SetStaticMesh(Quad);
		C->SetMaterial(0, WispMat);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
		C->SetVisibility(false);
		WispComps.Add(C);
	}

	SizzleAudio = ShadeAssets::Loop(RootComponent, TEXT("SFX_Sizzle_Loop"), 0.f, true);

	for (TActorIterator<AShadeInteractable> It(GetWorld()); It; ++It)
	{
		Interactables.Add(*It);
	}
	LastYaw = GetActorRotation().Yaw;
	SafeSpots.Add({ GetActorLocation(), GetActorRotation(), GetWorld()->GetTimeSeconds() });
}

// =====================================================================================================================
// Input API
// =====================================================================================================================

void AShadeCharacter::MoveInput(const FVector2D& Axis)
{
	if (!bControlEnabled || EvapState != 0 || bMerging)
	{
		return;
	}
	PendingMove = Axis;
	if (Grabbed.IsValid() || HoldTarget.IsValid())
	{
		return;
	}
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	AddMovementInput(Fwd, Axis.Y);
	AddMovementInput(Right, Axis.X);
}

void AShadeCharacter::LookInput(const FVector2D& Axis)
{
	if (EvapState == 1 || EvapState == 2)
	{
		return;
	}
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(Axis.Y);
	if (!Axis.IsNearlyZero())
	{
		LookIdle = 0.f;
	}
}

void AShadeCharacter::JumpInput()
{
	if (!bControlEnabled || EvapState != 0 || Grabbed.IsValid() || HoldTarget.IsValid() || bMerging)
	{
		return;
	}
	Jump();
}

void AShadeCharacter::SetSprint(bool bInSprint)
{
	bSprint = bInSprint;
}

void AShadeCharacter::InteractPressed(AShadeInteractable* Explicit)
{
	if (!bControlEnabled || EvapState != 0 || bMerging)
	{
		return;
	}
	if (Grabbed.IsValid())
	{
		ReleaseGrab();
		return;
	}
	AShadeInteractable* Target = Explicit ? Explicit : Focused.Get();
	if (!Target || !Target->CanInteract(this))
	{
		return;
	}
	if (Target->IsHoldInteraction())
	{
		HoldTarget = Target;
	}
	Target->Interact(this);
}

void AShadeCharacter::InteractReleased()
{
	if (AShadeInteractable* H = HoldTarget.Get())
	{
		H->EndInteract(this);
	}
	HoldTarget.Reset();
}

void AShadeCharacter::ToggleGrab(AShadeLemonCart* Cart)
{
	if (!Cart)
	{
		return;
	}
	if (Grabbed.Get() == Cart)
	{
		ReleaseGrab();
		return;
	}
	Grabbed = Cart;
	Cart->SetGrabbed(true);
	GrabBlend = 0.f;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	Rig->SetPose(ECatPose::Push, 0.3f);
	ShadeAssets::PlayAt(this, TEXT("SFX_Grab"), GetActorLocation(), 0.7f);
}

void AShadeCharacter::ReleaseGrab()
{
	if (AShadeLemonCart* C = Grabbed.Get())
	{
		C->SetGrabbed(false);
		ShadeAssets::PlayAt(this, TEXT("SFX_Release"), GetActorLocation(), 0.5f);
	}
	Grabbed.Reset();
	GetCharacterMovement()->bOrientRotationToMovement = true;
	Rig->SetPose(ECatPose::Locomote, 0.3f);
}

void AShadeCharacter::SetForcedFocus(AShadeInteractable* Target)
{
	ForcedFocus = Target;
}

void AShadeCharacter::SetControlEnabled(bool bEnabled)
{
	bControlEnabled = bEnabled;
	if (!bEnabled)
	{
		InteractReleased();
		PendingMove = FVector2D::ZeroVector;
	}
}

void AShadeCharacter::BeginMerge(const FVector& Spot, float Duration)
{
	bMerging = true;
	MergeSpot = Spot;
	MergeDuration = FMath::Max(Duration, 0.2f);
	MergeAlpha = 0.f;
	ReleaseGrab();
	InteractReleased();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
}

// =====================================================================================================================
// Tick
// =====================================================================================================================

void AShadeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Clamp(DeltaSeconds, 0.f, 0.05f);
	NoiseTime += Dt;

	GetCharacterMovement()->MaxWalkSpeed = (bSprint && !Grabbed.IsValid()) ? SprintSpeed : WalkSpeed;

	UpdateGrab(Dt);
	UpdateHold(Dt);
	UpdateSun(Dt);
	UpdateEvaporation(Dt);
	UpdateFocus(Dt);
	UpdateVisuals(Dt);
	UpdateWisps(Dt);
	UpdateCamera(Dt);
	PendingMove = FVector2D::ZeroVector;
}

void AShadeCharacter::GetSunPoints(TArray<FVector>& Out) const
{
	const FVector Base = GetActorLocation() - FVector(0.f, 0.f, GCapsuleHalf - 9.f);
	const FVector F = GetActorForwardVector();
	Out.Reset();
	Out.Add(Base + F * 15.f);
	Out.Add(Base);
	Out.Add(Base - F * 15.f);
}

void AShadeCharacter::UpdateSun(float Dt)
{
	UShadeSunSubsystem* Sun = UShadeSunSubsystem::Get(this);
	if (!Sun || EvapState != 0 || bMerging)
	{
		SunFraction = 0.f;
		return;
	}
	TArray<FVector> Pts;
	GetSunPoints(Pts);
	SunFraction = Sun->SunlitFraction(Pts, this, Grabbed.Get() ? nullptr : nullptr);

	const AShadeGameMode* G = GetShadeGameMode();
	const bool bLive = bControlEnabled && !bInvulnerable && (!G || G->GetPhase() == EShadePhase::Playing || G->GetPhase() == EShadePhase::Finale);
	if (bLive && SunFraction > 0.f)
	{
		Exposure += Dt * SunFraction / FMath::Max(BurnTime, 0.05f);
	}
	else
	{
		Exposure -= Dt / FMath::Max(CoolTime, 0.05f);
	}
	Exposure = FMath::Clamp(Exposure, 0.f, 1.f);
	if (Exposure >= 1.f)
	{
		Evaporate();
		return;
	}

	// Remember safe footing for respawn.
	TimeSinceSafe = SunFraction > 0.f ? TimeSinceSafe + Dt : 0.f;
	SafeSampleTimer -= Dt;
	if (SafeSampleTimer <= 0.f && SunFraction <= 0.f && !GetCharacterMovement()->IsFalling() && !Grabbed.IsValid())
	{
		SafeSampleTimer = 0.2f;
		// Only trust spots that are comfortably inside shade (all points + a wider ring are dark).
		TArray<FVector> Ring;
		const FVector C = GetActorLocation() - FVector(0.f, 0.f, GCapsuleHalf - 9.f);
		for (int32 i = 0; i < 6; ++i)
		{
			const float A = i * PI / 3.f;
			Ring.Add(C + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * 40.f);
		}
		if (Sun->SunlitFraction(Ring, this) <= 0.f)
		{
			SafeSpots.Add({ GetActorLocation(), GetActorRotation(), GetWorld()->GetTimeSeconds() });
			if (SafeSpots.Num() > 60)
			{
				SafeSpots.RemoveAt(0);
			}
		}
	}

	if (SizzleAudio)
	{
		SizzleAudio->SetVolumeMultiplier(FMath::Clamp(Exposure * 1.4f, 0.f, 1.f) * (SunFraction > 0.f ? 1.f : 0.5f));
		SizzleAudio->SetPitchMultiplier(0.9f + 0.35f * Exposure);
	}
}

void AShadeCharacter::Evaporate()
{
	if (EvapState != 0)
	{
		return;
	}
	EvapState = 1;
	EvapTimer = 0.f;
	++Evaporations;
	ReleaseGrab();
	InteractReleased();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	ShadeAssets::PlayAt(this, TEXT("SFX_Evaporate"), GetActorLocation(), 1.f);
	SpawnBurst(GetActorLocation(), 18, 1.f, 140.f);
	AddTrauma(0.45f);
	UE_LOG(LogShade, Log, TEXT("SHADE_EVENT evaporated at %s"), *GetActorLocation().ToCompactString());
	if (AShadeGameMode* G = GetShadeGameMode())
	{
		G->NotifyEvent(TEXT("Evaporated"), this);
	}
}

void AShadeCharacter::UpdateEvaporation(float Dt)
{
	if (EvapState == 0)
	{
		EvapAlpha = ShadeEase::Approach(EvapAlpha, 0.f, 6.f, Dt);
		return;
	}
	EvapTimer += Dt;
	if (EvapState == 1)
	{
		EvapAlpha = ShadeEase::Clamp01(EvapTimer / 0.5f);
		if (EvapTimer > 0.55f)
		{
			EvapState = 2;
			EvapTimer = 0.f;
			// Pick a spot remembered at least ~0.7 s ago (so it isn't right at the edge we burnt on).
			const double Now = GetWorld()->GetTimeSeconds();
			FSafeSpot Best = SafeSpots.Num() > 0 ? SafeSpots[0] : FSafeSpot{ GetActorLocation(), GetActorRotation(), Now };
			for (int32 i = SafeSpots.Num() - 1; i >= 0; --i)
			{
				if (Now - SafeSpots[i].Time >= 0.7)
				{
					Best = SafeSpots[i];
					break;
				}
			}
			SetActorLocation(Best.Loc, false, nullptr, ETeleportType::TeleportPhysics);
			SetActorRotation(Best.Rot);
			if (AController* C = GetController())
			{
				C->SetControlRotation(FRotator(-14.f, Best.Rot.Yaw, 0.f));
			}
			Exposure = 0.f;
		}
	}
	else if (EvapState == 2)
	{
		EvapAlpha = 1.f;
		if (EvapTimer > 0.4f)
		{
			EvapState = 3;
			EvapTimer = 0.f;
			ShadeAssets::PlayAt(this, TEXT("SFX_Reform"), GetActorLocation(), 0.8f);
			SpawnBurst(GetActorLocation(), 12, 0.f, -90.f);
		}
	}
	else if (EvapState == 3)
	{
		EvapAlpha = 1.f - ShadeEase::OutCubic(EvapTimer / 0.5f);
		if (EvapTimer > 0.5f)
		{
			EvapState = 0;
			GetCharacterMovement()->SetMovementMode(MOVE_Walking);
			if (AShadeGameMode* G = GetShadeGameMode())
			{
				G->NotifyEvent(TEXT("Reformed"), this);
			}
		}
	}
}

void AShadeCharacter::UpdateFocus(float Dt)
{
	AShadeInteractable* Best = nullptr;
	if (Grabbed.IsValid())
	{
		Best = Grabbed.Get();
	}
	else if (HoldTarget.IsValid())
	{
		Best = HoldTarget.Get();
	}
	else if (ForcedFocus.IsValid() && ForcedFocus->CanInteract(this))
	{
		Best = ForcedFocus.Get();
	}
	else if (bControlEnabled && EvapState == 0 && !bMerging)
	{
		float BestScore = TNumericLimits<float>::Max();
		const FVector Me = GetActorLocation();
		const FVector Fwd = GetActorForwardVector();
		for (const TWeakObjectPtr<AShadeInteractable>& W : Interactables)
		{
			AShadeInteractable* I = W.Get();
			if (!I || !I->CanInteract(this))
			{
				continue;
			}
			const FVector To = I->GetInteractLocation() - Me;
			const float D = FVector(To.X, To.Y, To.Z * 1.6f).Size();
			if (D > I->InteractRange + GCapsuleRadius)
			{
				continue;
			}
			const float Facing = FVector::DotProduct(Fwd, (I->GetFocusLocation() - Me).GetSafeNormal2D());
			const float Score = D * (1.4f - 0.4f * Facing);
			if (Score < BestScore)
			{
				BestScore = Score;
				Best = I;
			}
		}
	}
	if (Best != Focused.Get())
	{
		if (Best)
		{
			ShadeAssets::Play2D(this, TEXT("SFX_UI_Move"), 0.25f, 1.2f);
		}
		Focused = Best;
	}
	FocusAlpha = ShadeEase::Approach(FocusAlpha, Best ? 1.f : 0.f, 10.f, Dt);
}

void AShadeCharacter::UpdateGrab(float Dt)
{
	AShadeLemonCart* Cart = Grabbed.Get();
	if (!Cart)
	{
		return;
	}
	GrabBlend = FMath::Min(1.f, GrabBlend + Dt * 5.f);
	// Input projected onto the cart axis, camera-relative like normal movement.
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Wish = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * PendingMove.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * PendingMove.X;
	const FVector Axis = Cart->GetPushDirection();
	float Amount = FVector::DotProduct(Wish, Axis);
	if (FMath::Abs(Amount) < 0.25f)
	{
		Amount = 0.f;
	}
	const float Moved = GrabBlend >= 1.f ? Cart->Push(FMath::Clamp(Amount, -1.f, 1.f) * PushSpeed * Dt) : 0.f;

	const FVector Target = Cart->GetGrabLocation();
	FVector Loc = GetActorLocation();
	const FVector Want(Target.X, Target.Y, Loc.Z);
	Loc = GrabBlend < 1.f ? FMath::Lerp(Loc, Want, FMath::Min(1.f, Dt * 14.f)) : Want;
	SetActorLocation(Loc, false);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Axis.Rotation(), Dt, 12.f));
	GetCharacterMovement()->Velocity = Axis * (Moved / FMath::Max(Dt, 1e-4f));
	if (FVector::Dist2D(GetActorLocation(), Target) > 120.f)
	{
		ReleaseGrab();
	}
}

void AShadeCharacter::UpdateHold(float Dt)
{
	AShadeInteractable* H = HoldTarget.Get();
	if (!H)
	{
		return;
	}
	if (H->IsSolved() || FVector::Dist(GetActorLocation(), H->GetInteractLocation()) > H->InteractRange + 60.f)
	{
		InteractReleased();
		return;
	}
	// Face what we're tugging.
	const FVector To = (H->GetFocusLocation() - GetActorLocation()).GetSafeNormal2D();
	if (!To.IsNearlyZero())
	{
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), To.Rotation(), Dt, 10.f));
	}
}

void AShadeCharacter::UpdateVisuals(float Dt)
{
	const FVector V = GetVelocity();
	const float Yaw = GetActorRotation().Yaw;
	const float TurnRate = FMath::FindDeltaAngleDegrees(LastYaw, Yaw) / FMath::Max(Dt, 1e-4f);
	LastYaw = Yaw;

	Rig->Speed = V.Size2D();
	Rig->TurnRate = TurnRate;
	Rig->bGrounded = !GetCharacterMovement()->IsFalling();
	Rig->VerticalSpeed = V.Z;
	Rig->Excitement = FMath::Lerp(0.55f, 0.1f, Exposure);
	if (AShadeInteractable* F = Focused.Get())
	{
		Rig->bHasLook = true;
		Rig->LookAtWorld = F->GetFocusLocation();
	}
	else
	{
		Rig->bHasLook = false;
	}

	// Body material: ember edges while burning, dissolve while evaporating or merging.
	const float Pulse = 0.5f + 0.5f * FMath::Sin(NoiseTime * 2.2f);
	if (BodyMID)
	{
		BodyMID->SetScalarParameterValue(TEXT("Burn"), Exposure);
		BodyMID->SetScalarParameterValue(TEXT("Dissolve"), FMath::Max(EvapAlpha, MergeAlpha * 0.9f));
		BodyMID->SetScalarParameterValue(TEXT("Pulse"), Pulse);
	}
	if (EyeMID)
	{
		EyeMID->SetScalarParameterValue(TEXT("Glow"), (1.f - EvapAlpha) * (1.f - MergeAlpha) * (1.f + 1.5f * Exposure));
		EyeMID->SetScalarParameterValue(TEXT("Burn"), Exposure);
	}
	EyeLight->SetIntensity(18.f * (1.f - EvapAlpha) * (1.f - MergeAlpha) * (1.f + 2.f * Exposure));
	EyeLight->SetLightColor(FMath::Lerp(FLinearColor(1.f, 0.62f, 0.2f), FLinearColor(1.f, 0.9f, 0.7f), Exposure));

	// Squash into the ground while evaporating; pour flat while merging.
	float FlatZ = 1.f - 0.65f * EvapAlpha;
	if (bMerging)
	{
		MergeAlpha = FMath::Min(1.f, MergeAlpha + Dt / MergeDuration);
		const float A = ShadeEase::InOutCubic(MergeAlpha);
		const FVector Loc = FMath::Lerp(GetActorLocation(), FVector(MergeSpot.X, MergeSpot.Y, GetActorLocation().Z), FMath::Min(1.f, Dt * 2.5f));
		SetActorLocation(Loc, false);
		FlatZ = FMath::Lerp(1.f, 0.04f, A);
	}
	Body->SetRelativeScale3D(FVector(GMeshScale * (1.f + 0.2f * EvapAlpha), GMeshScale * (1.f + 0.2f * EvapAlpha), GMeshScale * FlatZ));
	Body->SetVisibility(EvapState != 2 && MergeAlpha < 0.999f);
	if (bMerging && MergeAlpha >= 1.f)
	{
		EyeLight->SetVisibility(false);
	}
}

void AShadeCharacter::SpawnWisp(const FVector& P, const FVector& V, float Size0, float Size1, float Life, float Heat, float Alpha)
{
	for (FShadeWisp& W : Wisps)
	{
		if (!W.bActive)
		{
			W = { P, V, 0.f, Life, Size0, Size1, Heat, Alpha, true };
			return;
		}
	}
}

void AShadeCharacter::SpawnBurst(const FVector& Center, int32 Count, float Heat, float Speed)
{
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Dir = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(0.2f, 1.f)).GetSafeNormal();
		const FVector P = Speed >= 0.f ? Center + Dir * 8.f : Center + Dir * 70.f;
		const FVector Vel = Speed >= 0.f ? Dir * Speed * FMath::FRandRange(0.5f, 1.2f) + FVector(0, 0, 60.f) : -Dir * FMath::Abs(Speed);
		const float Size1 = Heat > 0.5f ? 46.f : (Heat < 0.f ? 34.f : 16.f);
		SpawnWisp(P, Vel, 10.f, Size1, FMath::FRandRange(0.6f, 1.1f), Heat, Heat > 0.5f ? 0.65f : (Heat < 0.f ? 0.45f : 0.7f));
	}
}

void AShadeCharacter::UpdateWisps(float Dt)
{
	const float Sp = GetVelocity().Size2D();
	const FVector Base = GetActorLocation();
	const bool bVisible = EvapState == 0 && !bMerging;
	// Ink trail: shadows shed a little of themselves as they move.
	WispTimer -= Dt;
	if (bVisible && WispTimer <= 0.f)
	{
		WispTimer = FMath::Lerp(0.5f, 0.07f, FMath::Clamp(Sp / 450.f, 0.f, 1.f));
		const FVector P = Base + GetActorForwardVector() * FMath::FRandRange(-22.f, 8.f) + GetActorRightVector() * FMath::FRandRange(-8.f, 8.f) + FVector(0, 0, FMath::FRandRange(-6.f, 10.f));
		SpawnWisp(P, -GetVelocity() * 0.12f + FVector(FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(-6.f, 6.f), FMath::FRandRange(8.f, 22.f)),
			FMath::FRandRange(7.f, 10.f), FMath::FRandRange(15.f, 22.f), FMath::FRandRange(0.8f, 1.3f), 0.f, 0.42f);
	}
	// Burn smoke.
	SmokeTimer -= Dt;
	if (bVisible && Exposure > 0.04f && SmokeTimer <= 0.f)
	{
		SmokeTimer = FMath::Lerp(0.09f, 0.025f, Exposure);
		const FVector P = Base + GetActorForwardVector() * FMath::FRandRange(-18.f, 18.f) + FVector(0, 0, FMath::FRandRange(0.f, 14.f));
		SpawnWisp(P, FVector(FMath::FRandRange(-12.f, 12.f), FMath::FRandRange(-12.f, 12.f), FMath::FRandRange(70.f, 130.f)),
			FMath::FRandRange(8.f, 12.f), FMath::FRandRange(28.f, 44.f), FMath::FRandRange(0.6f, 0.9f), FMath::FRandRange(0.6f, 1.f), 0.5f);
	}

	const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector CamLoc = Cam ? Cam->GetCameraLocation() : Base + FVector(-300, 0, 100);
	for (int32 i = 0; i < Wisps.Num(); ++i)
	{
		FShadeWisp& W = Wisps[i];
		UStaticMeshComponent* C = WispComps.IsValidIndex(i) ? WispComps[i].Get() : nullptr;
		if (!C)
		{
			continue;
		}
		if (!W.bActive)
		{
			if (C->IsVisible()) { C->SetVisibility(false); }
			continue;
		}
		W.Age += Dt;
		if (W.Age >= W.Life)
		{
			W.bActive = false;
			C->SetVisibility(false);
			continue;
		}
		const float T = W.Age / W.Life;
		W.Vel *= FMath::Exp(-1.6f * Dt);
		W.Vel.Z += (W.Heat > 0.3f ? 40.f : (W.Heat < 0.f ? -30.f : 6.f)) * Dt;
		W.Pos += W.Vel * Dt;
		const float Size = FMath::Lerp(W.Size0, W.Size1, ShadeEase::OutCubic(T));
		C->SetWorldLocation(W.Pos);
		C->SetWorldRotation((CamLoc - W.Pos).Rotation());
		C->SetWorldScale3D(FVector(1.f, Size / 100.f, Size / 100.f));
		C->SetCustomPrimitiveDataFloat(0, W.Alpha * FMath::Sin(PI * FMath::Min(1.f, T * 1.15f)) * (1.f - T * 0.3f));
		C->SetCustomPrimitiveDataFloat(1, FMath::Max(W.Heat, 0.f) * (1.f - T));
		C->SetCustomPrimitiveDataFloat(2, W.Heat < 0.f ? 1.f : 0.f);
		if (!C->IsVisible()) { C->SetVisibility(true); }
	}
}

void AShadeCharacter::UpdateCamera(float Dt)
{
	Trauma = FMath::Max(0.f, Trauma - Dt * 1.4f);
	const float BurnShake = Exposure * 0.35f;
	const float Shake = FMath::Square(FMath::Max(Trauma, BurnShake));
	const FRotator Jitter(FMath::PerlinNoise1D(NoiseTime * 13.f) * 2.2f * Shake, FMath::PerlinNoise1D(NoiseTime * 11.f + 7.f) * 2.2f * Shake, FMath::PerlinNoise1D(NoiseTime * 9.f + 3.f) * 1.5f * Shake);
	FollowCamera->SetRelativeRotation(Jitter);
	const float Sp = GetVelocity().Size2D();
	const float SprintFov = FMath::Clamp((Sp - WalkSpeed) / (SprintSpeed - WalkSpeed), 0.f, 1.f) * 6.f;
	FollowCamera->SetFieldOfView(ShadeEase::Approach(FollowCamera->FieldOfView, BaseFOV + SprintFov - 3.f * Exposure, 4.f, Dt));
	// Pull the camera in a touch while pushing / tugging so the interaction reads.
	const float WantArm = (Grabbed.IsValid() || HoldTarget.IsValid()) ? 250.f : 285.f;
	CameraBoom->TargetArmLength = ShadeEase::Approach(CameraBoom->TargetArmLength, WantArm, 3.f, Dt);
}

void AShadeCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float Impact = FMath::Clamp(-GetVelocity().Z / 700.f, 0.f, 1.2f);
	Rig->Land(Impact);
	ShadeAssets::PlayAt(this, TEXT("SFX_Land"), GetActorLocation(), 0.25f + 0.5f * Impact);
	if (Impact > 0.3f)
	{
		for (int32 i = 0; i < 5; ++i)
		{
			const float A = i * 2.f * PI / 5.f;
			SpawnWisp(GetActorLocation() - FVector(0, 0, GCapsuleHalf - 4.f), FVector(FMath::Cos(A), FMath::Sin(A), 0.2f) * 80.f, 8.f, 20.f, 0.6f, 0.f, 0.4f);
		}
	}
}

void AShadeCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	ShadeAssets::PlayAt(this, TEXT("SFX_Jump"), GetActorLocation(), 0.45f);
}
