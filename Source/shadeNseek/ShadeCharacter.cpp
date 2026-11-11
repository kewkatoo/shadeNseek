#include "ShadeCharacter.h"
#include "shadeNseek.h"
#include "ShadeAssets.h"
#include "ShadeCatRig.h"
#include "ShadeGameMode.h"
#include "ShadeInteractables.h"
#include "ShadeSpringArm.h"
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
	Move->RotationRate = FRotator(0.f, 360.f, 0.f);
	Move->MaxWalkSpeed = WalkSpeed;
	Move->MaxAcceleration = 720.f;
	Move->BrakingDecelerationWalking = 950.f;
	Move->GroundFriction = 7.f;
	// ~1.1 m hop: clears the 87 cm planter wall with room to spare and reads as a cat's spring, not a launch.
	Move->JumpZVelocity = 500.f;
	Move->AirControl = 0.5f;  // a cat commits to its leap: a little steering in the air, not a full turn
	Move->MaxStepHeight = 26.f;
	Move->SetWalkableFloorAngle(52.f);
	Move->bCanWalkOffLedgesWhenCrouching = true;
	Move->PerchRadiusThreshold = 6.f;

	CameraBoom = CreateDefaultSubobject<UShadeSpringArm>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.f;
	// The boom pivots on the capsule centre (never inside the world, unlike a point above it that a hop can push
	// into the vault ceiling); the height lives in the socket offset instead.
	// 62 cm over a 3 m boom keeps Shade about 73% of the way down the frame: low enough to see the street ahead, high
	// enough that the narration at the bottom of the screen never covers the cat.
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 62.f);
	CameraBoom->TargetOffset = FVector::ZeroVector;
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
	// The rig reaches and stretches well outside the reference pose (hops, sprawls); generous bounds keep the
	// renderer from ever culling Shade while still on screen.
	Body->SetBoundsScale(1.6f);

	Rig = CreateDefaultSubobject<UShadeCatRig>(TEXT("Rig"));

	EyeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EyeLight"));
	EyeLight->SetupAttachment(Body);
	EyeLight->SetIntensityUnits(ELightUnits::Lumens);
	EyeLight->SetIntensity(0.4f);
	EyeLight->SetLightColor(FLinearColor(1.f, 0.62f, 0.2f));
	EyeLight->SetAttenuationRadius(26.f);
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
	BodyMID = ShadeAssets::SetCatMaterial(Body, TEXT("M_CatBody"), 0, TEXT("M_ShadeBody"));
	EyeMID = ShadeAssets::SetCatMaterial(Body, TEXT("M_CatEye"), 1, TEXT("M_ShadeEye"));
	WhiskerMID = ShadeAssets::SetCatMaterial(Body, TEXT("M_CatWhisker"), 2, TEXT("M_ShadeWhisker"));
	ShellMID = ShadeAssets::SetCatMaterial(Body, TEXT("M_CatShell"), 3, TEXT("M_ShadeShell"));
	Rig->Init(Body);
	Rig->MeshScale = GMeshScale;
	Rig->Excitement = 0.55f;
	Body->SetRelativeRotation(FRotator(0.f, Rig->GetFacingYaw(), 0.f));
	Body->SetRelativeScale3D(FVector(GMeshScale * Rig->GetUnitScale()));
	if (Rig->IsReady())
	{
		// Eye glow rides on the head bone.
		EyeLight->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("head"));
	}
	Rig->OnFootPlant = [this](int32 Leg)
	{
		const float Sp = GetVelocity().Size2D();
		ShadeAssets::PlayVariantAt(this, TEXT("SFX_Paw"), 6, GetActorLocation() - FVector(0, 0, GCapsuleHalf), FMath::Clamp(Sp / 300.f, 0.15f, 0.5f), 0.1f);
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
	if (Grabbed.IsValid() || HoldTarget.IsValid() || IsActBusy())
	{
		return;
	}
	Locomote(Axis, GetWorld() ? GetWorld()->GetDeltaSeconds() : 1.f / 60.f);
}

void AShadeCharacter::Locomote(const FVector2D& Axis, float Dt)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Wish = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * Axis.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * Axis.X;
	const float Mag = FMath::Min(Wish.Size2D(), 1.f);
	if (Mag < 0.05f)
	{
		return;
	}
	const FVector WishDir = Wish.GetSafeNormal2D();
	Move->bOrientRotationToMovement = false;
	const bool bAir = Move->IsFalling();
	const float Speed = GetVelocity().Size2D();
	// Nimble on the spot, wider arcs at a trot, only a little twist in the air.
	const float Quick = FMath::Clamp((Speed - 60.f) / FMath::Max(SprintSpeed - 60.f, 1.f), 0.f, 1.f);
	const float Rate = bAir ? 150.f : FMath::Lerp(720.f, 300.f, Quick);
	const float Cur = GetActorRotation().Yaw;
	const float Delta = FMath::FindDeltaAngleDegrees(Cur, WishDir.Rotation().Yaw);
	SetActorRotation(FRotator(0.f, Cur + FMath::Clamp(Delta, -Rate * Dt, Rate * Dt), 0.f));
	if (bAir)
	{
		AddMovementInput(WishDir, Mag);  // AirControl does the limiting
		return;
	}
	// It goes where its body points: the drive comes in as the body comes round (a sharp reversal pivots first), and the
	// legs are slow for a moment after a hard landing.
	const FVector Facing = GetActorForwardVector().GetSafeNormal2D();
	const float Align = FVector::DotProduct(Facing, WishDir);
	const float Recover = 1.f - 0.6f * FMath::Clamp(LandRecover / 0.3f, 0.f, 1.f);
	const float Drive = Mag * Recover * FMath::Clamp((Align + 0.17f) / 1.07f, 0.f, 1.f);
	AddMovementInput((Facing * 0.7f + WishDir * 0.3f).GetSafeNormal2D(), Drive);
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
	if (!bControlEnabled || EvapState != 0 || Grabbed.IsValid() || HoldTarget.IsValid() || bMerging || IsActBusy())
	{
		return;
	}
	// Cats gather themselves before they spring: a short crouch, then the jump.
	if (JumpWindup <= 0.f && !GetCharacterMovement()->IsFalling())
	{
		JumpWindup = 0.11f;
	}
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
	if (IsActBusy())
	{
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
	const EShadeAction Style = Target->GetActionStyle();
	if (Style == EShadeAction::None || Style == EShadeAction::Push)
	{
		// Instant, or the cart (its Interact grabs it, and the grab starts the push action).
		Target->Interact(this);
		return;
	}
	// Cords and the pot: Shade walks up and does it with its paws; Interact() fires when they get there.
	StartAction(Target);
}

void AShadeCharacter::InteractReleased()
{
	if (AShadeInteractable* H = HoldTarget.Get())
	{
		if (ActTarget.Get() == H && ActStage != EActStage::None)
		{
			if (bActFired)
			{
				H->EndInteract(this);
			}
			BeginLet();
		}
		else
		{
			H->EndInteract(this);
		}
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
	CartSpeed = 0.f;
	// Shade walks round to the tailboard, rears up and puts both paws on it before the cart moves (UpdateAction).
	StartAction(Cart);
}

void AShadeCharacter::ReleaseGrab()
{
	if (AShadeLemonCart* C = Grabbed.Get())
	{
		C->SetGrabbed(false);
		ShadeAssets::PlayAt(this, TEXT("SFX_Release"), GetActorLocation(), 0.5f);
	}
	const bool bWasActing = Grabbed.IsValid() && ActTarget.Get() == Grabbed.Get();
	Grabbed.Reset();
	CartSpeed = 0.f;
	Rig->SetPose(ECatPose::Locomote, 0.3f);
	if (bWasActing)
	{
		BeginLet();
	}
	else
	{
		GetCharacterMovement()->bOrientRotationToMovement = true;
	}
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
	EndScramble();
	bMerging = true;
	MergeSpot = Spot;
	MergeDuration = FMath::Max(Duration, 0.2f);
	MergeAlpha = 0.f;
	ReleaseGrab();
	InteractReleased();
	CancelAction();
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
	if (JumpWindup > 0.f)
	{
		JumpWindup -= Dt;
		if (JumpWindup <= 0.f && bControlEnabled && EvapState == 0)
		{
			Jump();
		}
	}

	LandRecover = FMath::Max(0.f, LandRecover - Dt);
	ScrambleCooldown = FMath::Max(0.f, ScrambleCooldown - Dt);
	UpdateAction(Dt);
	if (bScrambling || TryScramble())
	{
		UpdateScramble(Dt);
	}
	UpdateIdle(Dt);
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
	if (bLive)
	{
		Health = FMath::Max(0.f, Health - Dt * (IdleDrain + SunDrain * SunFraction));
		if (Health <= 0.f)
		{
			Fade();
			return;
		}
	}
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

void AShadeCharacter::Fade()
{
	// Out of shadow entirely: dissolve for good; the game mode reloads the walk.
	if (bFaded)
	{
		return;
	}
	bFaded = true;
	Health = 0.f;
	if (EvapState == 0)
	{
		Evaporate();
	}
	if (AShadeGameMode* G = GetShadeGameMode())
	{
		G->ShadeFaded();
	}
}

void AShadeCharacter::Evaporate()
{
	if (EvapState != 0)
	{
		return;
	}
	EndScramble();
	EvapState = 1;
	EvapTimer = 0.f;
	++Evaporations;
	Health = FMath::Max(0.f, Health - EvaporateCost);
	if (Health <= 0.f)
	{
		bFaded = true;
		if (AShadeGameMode* G = GetShadeGameMode())
		{
			G->ShadeFaded();
		}
	}
	ReleaseGrab();
	InteractReleased();
	CancelAction();
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
		if (EvapTimer > 0.55f && bFaded)
		{
			EvapState = 2;
			EvapTimer = 0.f;
		}
		else if (EvapTimer > 0.55f)
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
			Rig->ResetDynamics();
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
		if (EvapTimer > 0.4f && !bFaded)
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

// =====================================================================================================================
// Physical interactions: Shade walks up to the thing, turns to it, rears up, puts its paws on it and only then does it
// do anything - pushing the cart along, tugging a cord, swatting the pot. The paws are IK targets on the cat rig.
// =====================================================================================================================

bool AShadeCharacter::IsActBusy() const
{
	return bScrambling || ActStage == EActStage::Approach || ActStage == EActStage::Reach || ActStage == EActStage::Work;
}

void AShadeCharacter::StartAction(AShadeInteractable* Target)
{
	if (!Target)
	{
		return;
	}
	ActTarget = Target;
	ActStyle = Target->GetActionStyle();
	ActStage = EActStage::Approach;
	ActTime = ActStuck = ActCycle = ActRearFix = 0.f;
	bActFired = false;
	const FVector Me = GetActorLocation();
	ActContact = Target->GetContactLocation(Me);
	if (const AShadeLemonCart* Cart = Cast<AShadeLemonCart>(Target))
	{
		// Between the handles, facing along the cart, paws on the tailboard.
		ActFace = Cart->GetPushDirection().GetSafeNormal2D();
		ActSpot = Cart->GetGrabLocation();
		Rig->SetPose(ECatPose::Push, 0.4f);
	}
	else
	{
		FVector D = ActContact - Me;
		D.Z = 0.f;
		ActFace = D.IsNearlyZero() ? GetActorForwardVector().GetSafeNormal2D() : D.GetSafeNormal();
		// Close enough that the paws land well within reach (the rig knows its own arm length).
		const float Reach = Rig->GetPawReach() * (ActStyle == EShadeAction::Swat ? 1.05f : 0.95f);
		ActSpot = ActContact - ActFace * Reach;
	}
	ActSpot.Z = Me.Z;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AShadeCharacter::BeginLet()
{
	if (ActStage == EActStage::None || ActStage == EActStage::Let)
	{
		return;
	}
	ActStage = EActStage::Let;
	ActTime = 0.f;
	GetCharacterMovement()->bOrientRotationToMovement = !Grabbed.IsValid();
}

void AShadeCharacter::CancelAction()
{
	if (AShadeInteractable* T = ActTarget.Get())
	{
		T->SetPull(0.f);
		T->SetHold(FVector::ZeroVector, FVector::RightVector, 0.f);
	}
	ActTarget.Reset();
	ActStage = EActStage::None;
	ActW = ActPull = ActLean = ActRear = ActRearFix = 0.f;
	Rig->ActW = 0.f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
}

void AShadeCharacter::UpdateAction(float Dt)
{
	AShadeInteractable* T = ActTarget.Get();
	if (ActStage != EActStage::None && !T)
	{
		CancelAction();
	}
	if (ActStage == EActStage::None)
	{
		Rig->ActW = 0.f;
		return;
	}
	ActTime += Dt;
	const FVector Me = GetActorLocation();
	const float Feet = Me.Z - GCapsuleHalf;
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, ActFace).GetSafeNormal();
	const bool bCart = ActStyle == EShadeAction::Push;
	const bool bSwat = ActStyle == EShadeAction::Swat;
	const float ReachTime = bSwat ? 0.16f : 0.3f;
	ActContact = T->GetContactLocation(Me);

	switch (ActStage)
	{
	case EActStage::Approach:
	{
		FVector To = ActSpot - Me;
		To.Z = 0.f;
		const float D = To.Size();
		if (D > 3.f)
		{
			AddMovementInput(To / D, FMath::Clamp(D / 25.f, 0.3f, 1.f));
		}
		ActStuck = (D > 3.f && GetVelocity().Size2D() < 8.f) ? ActStuck + Dt : 0.f;
		if (D <= 3.f || ActStuck > 0.35f || ActTime > 1.8f)
		{
			// There: square up to it.
			GetCharacterMovement()->bOrientRotationToMovement = false;
			const FRotator Want = ActFace.Rotation();
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, Dt, 10.f));
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Want.Yaw)) < 8.f || ActTime > 2.4f)
			{
				GetCharacterMovement()->StopMovementImmediately();
				ActStage = EActStage::Reach;
				ActTime = 0.f;
			}
		}
		break;
	}
	case EActStage::Reach:
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), ActFace.Rotation(), Dt, 10.f));
		if (ActTime >= ReachTime)
		{
			ActStage = EActStage::Work;
			ActTime = 0.f;
			ActCycle = 0.f;
			if (bCart)
			{
				ShadeAssets::PlayAt(this, TEXT("SFX_Grab"), ActContact, 0.7f);
			}
			else if (ActStyle == EShadeAction::PullHold)
			{
				// Paws round the handle: now the cord actually works (released early = nothing happens).
				if (HoldTarget.Get() == T && T->CanInteract(this))
				{
					T->Interact(this);
					bActFired = true;
					ShadeAssets::PlayAt(this, TEXT("SFX_Grab"), ActContact, 0.45f);
				}
				else
				{
					BeginLet();
				}
			}
		}
		break;
	case EActStage::Work:
		if (ActStyle == EShadeAction::PullHold)
		{
			ActCycle += Dt / 0.85f;  // one tug every 0.85 s
		}
		else if (ActStyle == EShadeAction::PullTap)
		{
			// One hard yank: the shutter goes when the cord is well down.
			if (!bActFired && ActTime >= 0.2f)
			{
				if (T->CanInteract(this))
				{
					T->Interact(this);
				}
				bActFired = true;
			}
			if (ActTime >= 0.42f)
			{
				BeginLet();
			}
		}
		else if (bSwat)
		{
			// Paw cocked, then the swipe: the pot reacts when the paw meets it.
			if (!bActFired && ActTime >= 0.17f)
			{
				if (T->CanInteract(this))
				{
					T->Interact(this);
				}
				bActFired = true;
			}
			if (ActTime >= 0.34f)
			{
				BeginLet();
			}
		}
		break;
	case EActStage::Let:
		ActRearFix = ShadeEase::Approach(ActRearFix, 0.f, 5.f, Dt);
		if (ActTime >= 0.35f && ActW <= 0.03f)
		{
			T->SetPull(0.f);
			T->SetHold(FVector::ZeroVector, FVector::RightVector, 0.f);
			ActTarget.Reset();
			ActStage = EActStage::None;
			ActW = ActPull = ActLean = ActRear = ActRearFix = 0.f;
			Rig->ActW = 0.f;
			GetCharacterMovement()->bOrientRotationToMovement = !Grabbed.IsValid();
			return;
		}
		break;
	default:
		break;
	}

	// ---- What the paws and body do this frame ---------------------------------------------------------------------
	const bool bOn = ActStage == EActStage::Reach || ActStage == EActStage::Work;
	ActW = ShadeEase::Approach(ActW, bOn ? 1.f : 0.f, bOn ? (bSwat ? 14.f : 9.f) : 7.f, Dt);
	const float Tug = (ActStage == EActStage::Work && ActStyle == EShadeAction::PullHold)
		? (0.5f - 0.5f * FMath::Cos(2.f * PI * ActCycle)) * ShadeEase::Smooth(ActTime / 0.3f) : 0.f;
	float WantPull = 0.f;
	if (ActStage == EActStage::Work)
	{
		WantPull = ActStyle == EShadeAction::PullHold ? 3.f + 10.f * Tug
			: (ActStyle == EShadeAction::PullTap ? 16.f * ShadeEase::Smooth(ActTime / 0.25f) : 0.f);
	}
	ActPull = ActStyle == EShadeAction::PullHold && ActStage == EActStage::Work ? WantPull : ShadeEase::Approach(ActPull, WantPull, 16.f, Dt);
	T->SetPull(ActPull);

	// Height of the work above the ground decides how far up on the hind legs Shade goes (the handle's rest height for
	// cords, so the body sinks as it pulls the handle down; the paw balls sit a few cm above the bar they close round).
	const float WorkZ = ActContact.Z + (bCart || bSwat ? 0.f : ActPull + 4.f);
	float WantRear = bSwat ? 0.12f : FMath::Min(Rig->RearForHeight(WorkZ - Feet) + ActRearFix, 1.f);
	if (ActStyle == EShadeAction::PullHold || ActStyle == EShadeAction::PullTap)
	{
		WantRear *= 1.f - 0.3f * (ActStyle == EShadeAction::PullHold ? Tug : FMath::Min(ActPull / 16.f, 1.f));
	}
	ActRear = ShadeEase::Approach(ActRear, WantRear, 10.f, Dt);
	if (!bCart)
	{
		const float WantLean = ActStyle == EShadeAction::PullHold ? -0.2f - 0.6f * Tug
			: (ActStyle == EShadeAction::PullTap ? -0.8f * FMath::Min(ActPull / 16.f, 1.f)
			: (bSwat && ActStage == EActStage::Work && ActTime > 0.1f ? 0.5f : 0.f));
		ActLean = ShadeEase::Approach(ActLean, WantLean, 10.f, Dt);
	}
	else if (ActStage == EActStage::Reach)
	{
		ActLean = ShadeEase::Approach(ActLean, 0.3f, 8.f, Dt);
	}

	FVector PawL, PawR;
	float WL = 1.f, WR = 1.f, Meta = 135.f, Curl = 45.f;
	if (bCart)
	{
		// Both paws flat on the tailboard, toes up.
		PawL = ActContact - Right * 4.5f;
		PawR = ActContact + Right * 4.5f;
		Meta = 150.f;
		Curl = -70.f;
	}
	else if (bSwat)
	{
		// Right paw only: cocked back and up, then the swipe through the contact point.
		const FVector Cock = ActContact - ActFace * 9.f + Up * 8.f + Right * 2.f;
		const FVector Hit = ActContact + ActFace * 1.5f;
		const float S = ActStage == EActStage::Work ? ShadeEase::Smooth((ActTime - 0.09f) / 0.08f) : 0.f;
		PawR = FMath::Lerp(Cock, Hit, S);
		PawL = PawR;
		WL = 0.f;
		Meta = 95.f;
		Curl = 20.f;
	}
	else
	{
		// Both paws closed round the toggle's bar either side of the knot: each paw ball goes where that puts the bar in
		// the cup between the back of the paw and the curled toes.
		Rig->ActMeta = Meta;
		Rig->ActCurl = Curl;
		PawL = Rig->BallForGrip(ActContact - Right * 3.5f);
		PawR = Rig->BallForGrip(ActContact + Right * 3.5f);
	}
	Rig->ActW = ActW;
	Rig->ActRear = ActRear;
	Rig->ActLean = ActLean;
	Rig->ActMeta = Meta;
	Rig->ActCurl = Curl;
	Rig->ActPawW[0] = WL;
	Rig->ActPawW[1] = WR;
	Rig->ActPawWorld[0] = PawL;
	Rig->ActPawWorld[1] = PawR;

	if (!bCart && !bSwat)
	{
		// The toggle is in the paws: it goes where they actually are (last solve), never hanging in the air beside them.
		const FVector Grip = 0.5f * (Rig->GetGripWorld(0) + Rig->GetGripWorld(1));
		T->SetHold(Grip, Right, ShadeEase::Smooth((ActW - 0.7f) / 0.25f));
		if (ActStage == EActStage::Work && ActW > 0.95f)
		{
			// Paws arrived but short of the handle: shuffle in and come up higher on the hind legs until they close on it
			// (not while they are still on their way, or it would creep in on every reach).
			const FVector Gap = ActContact - Grip;
			const float Ahead = FVector::DotProduct(Gap, ActFace);
			if (Ahead > 1.5f)
			{
				AddActorWorldOffset(ActFace * FMath::Min(Ahead - 1.f, 30.f * Dt), true);
			}
			ActRearFix = FMath::Clamp(ActRearFix + FMath::Clamp(Gap.Z, -4.f, 6.f) * 0.06f * Dt, 0.f, 0.35f);
		}
	}
}

// =====================================================================================================================
// Ledge scramble and sitting
// =====================================================================================================================

bool AShadeCharacter::TryScramble()
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (bScrambling || ScrambleCooldown > 0.f || !Move->IsFalling() || !bControlEnabled || EvapState != 0 || bMerging
		|| ActStage != EActStage::None || Grabbed.IsValid() || HoldTarget.IsValid())
	{
		return false;
	}
	// Around the top of a hop (not shooting up past the edge, not dropping fast), pushing at the wall it faces.
	const FVector V = GetVelocity();
	if (V.Z > 260.f || V.Z < -320.f)
	{
		return false;
	}
	const FVector Face = GetActorForwardVector().GetSafeNormal2D();
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Wish = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * PendingMove.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * PendingMove.X;
	if (Wish.Size2D() < 0.3f || FVector::DotProduct(Wish.GetSafeNormal2D(), Face) < 0.5f)
	{
		return false;
	}
	UWorld* W = GetWorld();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(ShadeScramble), false, this);
	const FVector Loc = GetActorLocation();
	const float Feet = Loc.Z - GCapsuleHalf;
	FHitResult Wall;
	if (!W->LineTraceSingleByChannel(Wall, Loc, Loc + Face * (GCapsuleRadius + 22.f), ECC_Pawn, Q) || Wall.bStartPenetrating
		|| Wall.ImpactNormal.Z > 0.4f || FVector::DotProduct(Wall.ImpactNormal.GetSafeNormal2D(), -Face) < 0.6f)
	{
		return false;
	}
	// The top of it, a little in from the face: a walkable edge the paws reach, but only just above the feet (the hop
	// nearly made it - this smooths a near miss, it doesn't climb walls).
	const FVector In = Wall.ImpactPoint - Wall.ImpactNormal.GetSafeNormal2D() * 8.f;
	FHitResult Top;
	if (!W->LineTraceSingleByChannel(Top, FVector(In.X, In.Y, Feet + 60.f), FVector(In.X, In.Y, Feet - 2.f), ECC_Pawn, Q) || Top.bStartPenetrating
		|| Top.ImpactNormal.Z < 0.75f)
	{
		return false;
	}
	const float Rise = Top.ImpactPoint.Z - Feet;
	if (Rise < 6.f || Rise > 24.f)
	{
		return false;
	}
	// Somewhere to stand on top, a body length in from the edge, with room for the whole cat.
	const FVector Edge(Wall.ImpactPoint.X, Wall.ImpactPoint.Y, Top.ImpactPoint.Z);
	const FVector Stand = Edge + Face * (GCapsuleRadius + 3.f);
	FHitResult Floor;
	if (!W->LineTraceSingleByChannel(Floor, Stand + FVector(0, 0, 30.f), Stand - FVector(0, 0, 8.f), ECC_Pawn, Q) || Floor.ImpactNormal.Z < 0.75f
		|| FMath::Abs(Floor.ImpactPoint.Z - Top.ImpactPoint.Z) > 6.f)
	{
		return false;
	}
	const FVector Dest(Stand.X, Stand.Y, Floor.ImpactPoint.Z + GCapsuleHalf + 1.5f);
	if (W->OverlapBlockingTestByChannel(Dest, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(GCapsuleRadius - 1.f, GCapsuleHalf - 1.f), Q))
	{
		return false;
	}
	// The way there must be open: straight up beside the wall, then over the edge (an overlap test alone can't tell a
	// spot inside a thick triangle-mesh wall from open air).
	const FVector Raised(Loc.X, Loc.Y, Dest.Z);
	FHitResult Path;
	if (W->LineTraceSingleByChannel(Path, Loc, Raised + FVector(0, 0, GCapsuleHalf * 0.5f), ECC_Pawn, Q)
		|| W->LineTraceSingleByChannel(Path, Raised, Dest, ECC_Pawn, Q)
		|| W->LineTraceSingleByChannel(Path, Raised - FVector(0, 0, GCapsuleHalf * 0.5f), Dest - FVector(0, 0, GCapsuleHalf * 0.5f), ECC_Pawn, Q))
	{
		return false;
	}
	bScrambling = true;
	ScrambleT = 0.f;
	ScrambleFrom = Loc;
	ScrambleTo = Dest;
	ScrambleEdge = Edge;
	ScrambleFace = Face;
	Move->StopMovementImmediately();
	Move->SetMovementMode(MOVE_Flying);
	ShadeAssets::PlayAt(this, TEXT("SFX_Land"), Edge, 0.3f, 1.25f);
	return true;
}

void AShadeCharacter::UpdateScramble(float Dt)
{
	if (!bScrambling)
	{
		return;
	}
	ScrambleT = FMath::Min(1.f, ScrambleT + Dt / 0.36f);
	const float T = ScrambleT;
	// Up first (front paws on the edge, hind legs scrabbling at the wall), then over the top.
	const float Up = ShadeEase::OutCubic(FMath::Clamp(T / 0.6f, 0.f, 1.f));
	const float Over = ShadeEase::InOutCubic(FMath::Clamp((T - 0.35f) / 0.65f, 0.f, 1.f));
	FVector P = FMath::Lerp(ScrambleFrom, ScrambleTo, Over);
	P.Z = FMath::Lerp(ScrambleFrom.Z, ScrambleTo.Z, Up);
	SetActorLocation(P, false);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), ScrambleFace.Rotation(), Dt, 14.f));
	const FVector Right = FVector::CrossProduct(FVector::UpVector, ScrambleFace).GetSafeNormal();
	Rig->ActW = 1.f - ShadeEase::Smooth((T - 0.62f) / 0.38f);
	Rig->ActRear = 0.5f * (1.f - Over);
	Rig->ActLean = 0.45f;
	Rig->ActMeta = 150.f;
	Rig->ActCurl = -55.f;
	Rig->ActPawW[0] = Rig->ActPawW[1] = 1.f;
	Rig->ActPawWorld[0] = ScrambleEdge + ScrambleFace * 4.f - Right * 4.f + FVector(0, 0, 1.f);
	Rig->ActPawWorld[1] = ScrambleEdge + ScrambleFace * 4.f + Right * 4.f + FVector(0, 0, 1.f);
	if (T >= 1.f)
	{
		EndScramble();
	}
}

void AShadeCharacter::EndScramble()
{
	if (!bScrambling)
	{
		return;
	}
	bScrambling = false;
	ScrambleCooldown = 0.4f;
	Rig->ActW = 0.f;
	if (GetCharacterMovement()->MovementMode == MOVE_Flying)
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
}

void AShadeCharacter::UpdateIdle(float Dt)
{
	const bool bStill = bControlEnabled && EvapState == 0 && !bMerging && !bScrambling && JumpWindup <= 0.f
		&& !GetCharacterMovement()->IsFalling() && GetVelocity().Size2D() < 4.f && PendingMove.IsNearlyZero()
		&& ActStage == EActStage::None && !Grabbed.IsValid() && !HoldTarget.IsValid();
	IdleTime = bStill ? IdleTime + Dt : 0.f;
	if (!bSitting && IdleTime > 5.f && Exposure < 0.05f)
	{
		bSitting = true;
		Rig->SetPose(ECatPose::Sit, 0.9f);
	}
	else if (bSitting && !bStill)
	{
		bSitting = false;
		if (Rig->GetPose() == ECatPose::Sit)
		{
			Rig->SetPose(ECatPose::Locomote, 0.25f);
		}
	}
}

void AShadeCharacter::UpdateGrab(float Dt)
{
	AShadeLemonCart* Cart = Grabbed.Get();
	if (!Cart || ActTarget.Get() != Cart || ActStage != EActStage::Work)
	{
		return;  // still walking round to the tailboard / rearing up (UpdateAction)
	}
	// Input projected onto the cart axis, camera-relative like normal movement.
	const FRotator Yaw(0.f, GetControlRotation().Yaw, 0.f);
	const FVector Wish = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * PendingMove.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * PendingMove.X;
	const FVector Axis = Cart->GetPushDirection();
	float Amount = FVector::DotProduct(Wish, Axis);
	if (FMath::Abs(Amount) < 0.25f)
	{
		Amount = 0.f;
	}
	// A loaded cart: it takes a shove to get going, rolls on a little when Shade eases off, and drags back slowly
	// (claws hooked over the tailboard).
	const float Want = FMath::Clamp(Amount, -0.5f, 1.f) * PushSpeed;
	const float Accel = FMath::Abs(Want) > FMath::Abs(CartSpeed) ? 160.f : 240.f;
	CartSpeed = FMath::FInterpConstantTo(CartSpeed, Want, Dt, Accel);
	const float Moved = Cart->Push(CartSpeed * Dt);
	if (FMath::Abs(Moved) < FMath::Abs(CartSpeed * Dt) * 0.5f)
	{
		CartSpeed = Moved / FMath::Max(Dt, 1e-4f);  // stopped at the end of the track
	}
	// Shade walks with the cart, paws staying on the tailboard; it leans in hardest while getting the cart moving.
	const FVector Target = Cart->GetGrabLocation();
	SetActorLocation(FVector(Target.X, Target.Y, GetActorLocation().Z), false);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Axis.Rotation(), Dt, 12.f));
	GetCharacterMovement()->Velocity = Axis * (Moved / FMath::Max(Dt, 1e-4f));
	const float Effort = Want > 0.f ? 0.45f + 0.55f * FMath::Clamp((Want - CartSpeed) / 50.f, 0.f, 1.f) : (Want < 0.f ? -0.55f : 0.12f);
	ActLean = ShadeEase::Approach(ActLean, Effort, 6.f, Dt);
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
	if (ActTarget.Get() == H)
	{
		return;  // UpdateAction walks up to it and faces it
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

	Rig->Velocity = V;
	Rig->TurnRate = TurnRate;
	Rig->bGrounded = !GetCharacterMovement()->IsFalling();
	Rig->Excitement = FMath::Lerp(0.6f, 0.05f, Exposure);
	Rig->Crouch = JumpWindup > 0.f ? 1.f : 0.f;
	if (ActStage != EActStage::None && ActW > 0.2f)
	{
		Rig->bHasLook = true;
		Rig->LookAtWorld = ActContact;
	}
	else if (AShadeInteractable* F = Focused.Get())
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
	const float Dissolve = FMath::Max(EvapAlpha, MergeAlpha * 0.9f);
	for (UMaterialInstanceDynamic* MID : { BodyMID.Get(), ShellMID.Get(), WhiskerMID.Get() })
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Burn"), Exposure);
			MID->SetScalarParameterValue(TEXT("Dissolve"), Dissolve);
			MID->SetScalarParameterValue(TEXT("Pulse"), Pulse);
		}
	}
	if (EyeMID)
	{
		// The eyes flare white-hot as Shade burns; they stay open (closed lids read as empty sockets).
		EyeMID->SetScalarParameterValue(TEXT("Glow"), (1.f - EvapAlpha) * (1.f - MergeAlpha) * (1.f + 1.5f * Exposure));
		EyeMID->SetScalarParameterValue(TEXT("Burn"), Exposure);
		EyeMID->SetScalarParameterValue(TEXT("Lid"), 0.f);
	}
	EyeLight->SetIntensity(0.4f * (1.f - EvapAlpha) * (1.f - MergeAlpha) * (1.f + 3.f * Exposure));
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
	const float U = GMeshScale * Rig->GetUnitScale();
	Body->SetRelativeScale3D(FVector(U * (1.f + 0.2f * EvapAlpha), U * (1.f + 0.2f * EvapAlpha), U * FlatZ));
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
	const float WantArm = (Grabbed.IsValid() || HoldTarget.IsValid()) ? 270.f : 300.f;
	CameraBoom->TargetArmLength = ShadeEase::Approach(CameraBoom->TargetArmLength, WantArm, 3.f, Dt);
	// Keep the (gentle) depth of field focused on Shade however far the boom is pushed in.
	FollowCamera->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
	FollowCamera->PostProcessSettings.DepthOfFieldFocalDistance = FMath::Max(60.f, FVector::Dist(FollowCamera->GetComponentLocation(), GetActorLocation()));
}

void AShadeCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	const float Impact = FMath::Clamp(-GetVelocity().Z / 700.f, 0.f, 1.2f);
	Rig->Land(Impact);
	if (Impact > 0.85f)
	{
		LandRecover = 0.32f * FMath::Min(Impact, 1.f);  // a real drop (well past an ordinary hop): legs take it, a beat to recover
	}
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
	// A running cat leaps long: a little extra reach along the body at a trot (a standing hop goes straight up).
	const float Run = FMath::Clamp((GetVelocity().Size2D() - WalkSpeed) / FMath::Max(SprintSpeed - WalkSpeed, 1.f), 0.f, 1.f);
	GetCharacterMovement()->Velocity += GetActorForwardVector().GetSafeNormal2D() * 40.f * Run;
}
