#include "ShadeGameMode.h"
#include "shadeNseek.h"
#include "ShadeAssets.h"
#include "ShadeCatRig.h"
#include "ShadeCharacter.h"
#include "ShadeHUD.h"
#include "ShadeInteractables.h"
#include "ShadePlayerController.h"
#include "ShadeSunSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#define LOCTEXT_NAMESPACE "ShadeGame"

AShadeGameMode::AShadeGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = false;
	DefaultPawnClass = AShadeCharacter::StaticClass();
	PlayerControllerClass = AShadePlayerController::StaticClass();
	HUDClass = AShadeHUD::StaticClass();
}

bool AShadeGameMode::HasParam(const TCHAR* Param)
{
	return FParse::Param(FCommandLine::Get(), Param);
}

bool AShadeGameMode::IsAutoplay()
{
	return HasParam(TEXT("autoplay"));
}

bool AShadeGameMode::IsAutoplayExit()
{
	return HasParam(TEXT("autoplayexit"));
}

void AShadeGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// A retry after Shade faded comes straight back into the lane (no title).
	bRetry = UGameplayStatics::HasOption(Options, TEXT("retry"));
	bSkipTitle = HasParam(TEXT("skiptitle")) || bRetry;
	if (FParse::Value(FCommandLine::Get(), TEXT("motioncap="), MotionCap) && MotionCap != TEXT("title"))
	{
		bSkipTitle = true;
	}
	BuildObjectives();
}

void AShadeGameMode::BuildObjectives()
{
	auto Add = [this](const TCHAR* On, const TCHAR* Target, const FText& Title, const FText& Hint)
	{
		FShadeObjective O;
		O.CompleteOn = On;
		O.Target = Target;
		O.Title = Title;
		O.Hint = Hint;
		Objectives.Add(O);
	};
	Add(TEXT("Zone_Lane"), TEXT("OBJ_Lane"), LOCTEXT("O0", "Find Biscotto"), LOCTEXT("O0H", "He naps up by the bell tower. Stay in the shade - sunlight burns."));
	Add(TEXT("Zone_AfterAlley"), TEXT("OBJ_Cart"), LOCTEXT("O1", "Cross the sunlit alley"), LOCTEXT("O1H", "Push the lemon cart into the sunbeam and walk in its shade."));
	Add(TEXT("Zone_AfterPiazza"), TEXT("OBJ_Awning"), LOCTEXT("O2", "Shade the little piazza"), LOCTEXT("O2H", "Hold E on the awning cord to roll the canvas out."));
	Add(TEXT("Zone_Terrace"), TEXT("OBJ_Pot"), LOCTEXT("O3", "Climb the old steps"), LOCTEXT("O3H", "Hop onto the wall and nudge the potted lemon tree off the edge."));
	Add(TEXT("TowerShadeArrived"), TEXT("OBJ_Wait"), LOCTEXT("O4", "Wait for the bell tower's shadow"), LOCTEXT("O4H", "The sun is sinking. Stay in the tower's shade and let it carry you to him."));
	Add(TEXT("Reunited"), TEXT("OBJ_Biscotto"), LOCTEXT("O5", "Reunite with Biscotto"), LOCTEXT("O5H", "Slip into his shadow - press E beside him."));

	EventLines.Add(TEXT("FirstBurn"), LOCTEXT("L_Burn", "Ouch! Sunlight is no place for a shadow."));
	EventLines.Add(TEXT("Evaporated"), LOCTEXT("L_Evap", "Poof... back to the last cool spot."));
	EventLines.Add(TEXT("CartSolved"), LOCTEXT("L_Cart", "A cart full of lemons makes a fine patch of shade."));
	EventLines.Add(TEXT("AwningSolved"), LOCTEXT("L_Awning", "Signora Rosa won't mind. Probably."));
	EventLines.Add(TEXT("PotSolved"), LOCTEXT("L_Pot", "Cats do love knocking things off ledges."));
	EventLines.Add(TEXT("Zone_Terrace"), LOCTEXT("L_Terrace", "Six o'clock... the bell tower's shadow is stretching across the terrace."));
	EventLines.Add(TEXT("TowerShadeArrived"), LOCTEXT("L_Tower", "There he is - still snoring, still shadowless."));
	EventLines.Add(TEXT("Zone_Lane"), LOCTEXT("L_Lane", "The cool side of the street. Keep to it."));
}

void AShadeGameMode::StartPlay()
{
	Super::StartPlay();
	// Uncooked runs print asset-compilation progress on screen; never show engine debug text over the game.
	if (APlayerController* PC0 = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC0->ConsoleCommand(TEXT("DisableAllScreenMessages"), true);
	}
	// Only big structures (houses, walls, ground, the tower) may push the follow camera in. Pots, shutters, sills,
	// lamps, chairs and the cart used to catch the boom in the narrow lanes and jerk the whole view back and forth.
	{
		int32 Freed = 0;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (Cast<APawn>(*It))
			{
				continue;
			}
			TInlineComponentArray<UPrimitiveComponent*> Prims(*It);
			for (UPrimitiveComponent* C : Prims)
			{
				if (C && C->IsQueryCollisionEnabled() && C->GetCollisionResponseToChannel(ECC_Camera) != ECR_Ignore && C->Bounds.SphereRadius < 240.f)
				{
					C->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
					++Freed;
				}
			}
		}
		UE_LOG(LogShade, Log, TEXT("SHADE_CAMERA %d small props no longer block the camera"), Freed);
	}
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("TitleCam")))
		{
			TitleCamera = *It;
			TitleCameraBase = It->GetActorTransform();
		}
	}
	for (TActorIterator<AShadeNapCat> It(GetWorld()); It; ++It)
	{
		if (It->bIsBiscotto)
		{
			Biscotto = *It;
		}
	}
	if (UShadeSunSubsystem* SS = UShadeSunSubsystem::Get(this))
	{
		Sun = SS->GetSun();
		if (Sun.IsValid())
		{
			SunStart = Sun->GetActorRotation();
		}
	}
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("SunFinale")))
		{
			SunEnd = It->GetActorRotation();
			bHasSunEnd = true;
		}
	}

	if (USoundBase* S = ShadeAssets::Sound(TEXT("MUS_Theme")))
	{
		Music = UGameplayStatics::SpawnSound2D(this, S, 0.f, 1.f, 0.f, nullptr, true, false);
	}
	if (USoundBase* S = ShadeAssets::Sound(TEXT("AMB_Sea_Loop")))
	{
		AmbSea = UGameplayStatics::SpawnSound2D(this, S, 0.32f, 1.f, 0.f, nullptr, true, false);
	}
	if (USoundBase* S = ShadeAssets::Sound(TEXT("AMB_Village_Loop")))
	{
		AmbVillage = UGameplayStatics::SpawnSound2D(this, S, 0.42f, 1.f, 0.f, nullptr, true, false);
	}
	MusicTarget = 0.5f;

	SetPhase(EShadePhase::Title);
	FadeAlpha = 1.f;
	FadeTo(0.f, 2.2f, FLinearColor::Black, 0.3f);
	if (TitleCamera)
	{
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			PC->SetViewTarget(TitleCamera);
		}
	}
	if (HasParam(TEXT("catgallery")))
	{
		RunCatGallery();
	}
	if (HasParam(TEXT("camshots")))
	{
		RunCamShots();
	}
	if (HasParam(TEXT("sunmap")))
	{
		bSunMapPending = true;
		SunMapTimer = 2.5f;
	}
	if (bSkipTitle)
	{
		StartGame();
	}
	UE_LOG(LogShade, Log, TEXT("SHADE_T0 %s"), *FDateTime::UtcNow().ToIso8601());
}

AShadeCharacter* AShadeGameMode::GetShade() const
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	return PC ? Cast<AShadeCharacter>(PC->GetPawn()) : nullptr;
}

void AShadeGameMode::SetPhase(EShadePhase NewPhase)
{
	Phase = NewPhase;
	PhaseStartRealTime = FPlatformTime::Seconds();
	PhaseTime = 0.f;
	UE_LOG(LogShade, Log, TEXT("SHADE_PHASE %d t=%.1f"), static_cast<int32>(NewPhase), RunTime);
	if (AShadePlayerController* PC = Cast<AShadePlayerController>(UGameplayStatics::GetPlayerController(this, 0)))
	{
		PC->OnPhaseChanged(NewPhase);
	}
	if (AShadeCharacter* S = GetShade())
	{
		S->SetControlEnabled(NewPhase == EShadePhase::Playing || NewPhase == EShadePhase::Finale);
	}
}

void AShadeGameMode::StartGame()
{
	if (Phase != EShadePhase::Title)
	{
		return;
	}
	ShadeAssets::Play2D(this, TEXT("SFX_UI_Select"), 0.7f);
	SetPhase(EShadePhase::Intro);
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (AShadeCharacter* S = GetShade())
		{
			PC->SetControlRotation(FRotator(-12.f, S->GetActorRotation().Yaw, 0.f));
			if (bSkipTitle)
			{
				PC->SetViewTarget(S);
			}
			else
			{
				// Dip to black and cut down to Shade. A view-target blend flies in a straight line from the rooftop
				// shot to the passage - straight through the houses, which flashed walls and rooms past the lens.
				const FLinearColor Dusk(0.02f, 0.015f, 0.03f);
				FadeTo(1.f, 0.7f, Dusk);
				TWeakObjectPtr<AShadeGameMode> Self(this);
				TWeakObjectPtr<APlayerController> WPC(PC);
				TWeakObjectPtr<AShadeCharacter> WS(S);
				FTimerHandle H;
				GetWorldTimerManager().SetTimer(H, [Self, WPC, WS, Dusk]()
				{
					if (Self.IsValid() && WPC.IsValid() && WS.IsValid())
					{
						WPC->SetViewTarget(WS.Get());
						Self->FadeTo(0.f, 1.3f, Dusk);
					}
				}, 0.78f, false);
			}
		}
	}
	if (bRetry)
	{
		Say(LOCTEXT("Retry1", "Back in the cool of the passage. Mind the sun this time."), 3.6f);
	}
	else
	{
		Say(LOCTEXT("Intro1", "Biscotto dozed off in the noon sun... and his shadow slipped away."), 4.6f);
	}
}

void AShadeGameMode::ShadeFaded()
{
	// Out of shadow: Shade is gone. Fade out and start the walk again from the passage.
	if (bFading || Phase == EShadePhase::Reunion || Phase == EShadePhase::Ended)
	{
		return;
	}
	bFading = true;
	UE_LOG(LogShade, Log, TEXT("SHADE_EVENT faded t=%.1f"), RunTime);
	Say(LOCTEXT("Faded", "The sun drank the last of Shade... Back to the cool of the passage."), 3.2f);
	FadeTo(1.f, 1.4f, FLinearColor(0.02f, 0.015f, 0.03f), 1.2f);
	MusicTarget = 0.f;
	FTimerHandle H;
	TWeakObjectPtr<AShadeGameMode> Self(this);
	GetWorldTimerManager().SetTimer(H, [Self]()
	{
		if (Self.IsValid())
		{
			UGameplayStatics::OpenLevel(Self.Get(), FName(*UGameplayStatics::GetCurrentLevelName(Self.Get())), true, IsAutoplay() ? TEXT("?retry?autoplay") : TEXT("?retry"));
		}
	}, 3.0f, false);
}

void AShadeGameMode::RestartRun()
{
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, IsAutoplay() ? TEXT("?autoplay") : TEXT(""));
}

void AShadeGameMode::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0), EQuitPreference::Quit, false);
}

void AShadeGameMode::Say(const FText& InLine, float Hold)
{
	Line = InLine;
	LineStartTime = FPlatformTime::Seconds();
	LineHold = Hold;
	UE_LOG(LogShade, Log, TEXT("SHADE_SAY %s"), *InLine.ToString());
}

void AShadeGameMode::PushToast(const FText& Text)
{
	FShadeToast T;
	T.Text = Text;
	T.StartTime = FPlatformTime::Seconds();
	Toasts.Add(T);
	if (Toasts.Num() > 3)
	{
		Toasts.RemoveAt(0);
	}
}

void AShadeGameMode::FadeTo(float Target, float Duration, const FLinearColor& Color, float Delay)
{
	FadeTarget = Target;
	FadeSpeed = 1.f / FMath::Max(Duration, 0.01f);
	FadeColor = Color;
	FadeDelay = Delay;
}

void AShadeGameMode::AddTrauma(float Amount)
{
	if (AShadeCharacter* S = GetShade())
	{
		S->AddTrauma(Amount);
	}
}

void AShadeGameMode::SpawnDust(const FVector& Location, float Strength)
{
	if (AShadeCharacter* S = GetShade())
	{
		S->SpawnBurst(Location, FMath::RoundToInt(10 * Strength), -1.f, 120.f * Strength);
	}
}

bool AShadeGameMode::CanReunite() const
{
	return HasEvent(TEXT("TowerShadeArrived")) && Phase == EShadePhase::Finale;
}

void AShadeGameMode::NotifyEvent(FName EventName, AActor* Source)
{
	if (EventName.IsNone())
	{
		return;
	}
	const bool bNew = !Events.Contains(EventName);
	Events.Add(EventName);
	if (bNew)
	{
		UE_LOG(LogShade, Log, TEXT("SHADE_EVENT %s t=%.1f"), *EventName.ToString(), RunTime);
		if (const FText* L = EventLines.Find(EventName))
		{
			Say(*L, 4.2f);
		}
	}
	if (EventName == TEXT("CartSolved") || EventName == TEXT("AwningSolved") || EventName == TEXT("PotSolved"))
	{
		if (bNew)
		{
			++PuzzlesSolved;
			ShadeAssets::Play2D(this, TEXT("SFX_Solve"), 0.6f);
			PushToast(FText::Format(LOCTEXT("Solved", "{0} of 3"), FText::AsNumber(PuzzlesSolved)));
		}
	}
	if (EventName == TEXT("Zone_Terrace") && bNew && Phase == EShadePhase::Playing)
	{
		SetPhase(EShadePhase::Finale);
		FinaleTime = 0.f;
		BellsRung = 0;
		MusicTarget = 0.28f;
	}
	if (EventName == TEXT("Evaporated") && !bNew)
	{
		// Repeat evaporations still get a gentle reminder now and then.
		if (FMath::FRand() < 0.4f) { Say(*EventLines.Find(TEXT("Evaporated")), 2.5f); }
	}
	AdvanceObjectives();
}

void AShadeGameMode::AdvanceObjectives()
{
	const int32 Before = ObjectiveIndex;
	while (Objectives.IsValidIndex(ObjectiveIndex) && Events.Contains(Objectives[ObjectiveIndex].CompleteOn))
	{
		++ObjectiveIndex;
	}
	if (ObjectiveIndex != Before)
	{
		ObjectiveChangeTime = FPlatformTime::Seconds();
		if (Objectives.IsValidIndex(ObjectiveIndex))
		{
			ShadeAssets::Play2D(this, TEXT("SFX_Objective"), 0.5f);
		}
	}
}

bool AShadeGameMode::GetObjectiveTargetLocation(FVector& Out) const
{
	const FShadeObjective* O = GetObjective();
	if (!O || O->Target.IsNone())
	{
		return false;
	}
	if (CachedTargetIndex != ObjectiveIndex)
	{
		CachedTarget.Reset();
		CachedTargetIndex = ObjectiveIndex;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(O->Target))
			{
				CachedTarget = *It;
				break;
			}
		}
	}
	if (const AActor* A = CachedTarget.Get())
	{
		Out = A->GetActorLocation();
		return true;
	}
	return false;
}

// =====================================================================================================================
// Tick
// =====================================================================================================================

void AShadeGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.1f);
	PhaseTime += Dt;
	if (Phase != EShadePhase::Title)
	{
		RunTime += Dt;
	}

	// Fade
	if (FadeDelay > 0.f)
	{
		FadeDelay -= Dt;
	}
	else
	{
		FadeAlpha = FMath::FInterpConstantTo(FadeAlpha, FadeTarget, Dt, FadeSpeed);
	}
	LetterboxTarget = (Phase == EShadePhase::Reunion || Phase == EShadePhase::Intro) ? 1.f : 0.f;
	Letterbox = FMath::FInterpConstantTo(Letterbox, LetterboxTarget, Dt, 1.2f);

	// Music
	MusicLevel = ShadeEase::Approach(MusicLevel, MusicTarget, 0.8f, Dt);
	if (Music)
	{
		Music->SetVolumeMultiplier(MusicLevel);
	}

	// Toast expiry
	const double Now = FPlatformTime::Seconds();
	Toasts.RemoveAll([Now](const FShadeToast& T) { return Now - T.StartTime > 4.0; });

	switch (Phase)
	{
	case EShadePhase::Title:
		TickTitle(Dt);
		break;
	case EShadePhase::Intro:
		// Keep the rooftop shot gently pushing in while it fades out.
		if (TitleCamera && PhaseTime < 0.8f && !bSkipTitle)
		{
			TitleCamera->AddActorWorldOffset(TitleCamera->GetActorForwardVector() * 140.f * Dt);
		}
		if (PhaseTime > (bSkipTitle ? 0.1f : 2.8f))
		{
			SetPhase(EShadePhase::Playing);
			if (!bRetry)
			{
				Say(LOCTEXT("Intro2", "Now the sun is low and the streets are blazing. Keep to the shade."), 4.6f);
			}
			ObjectiveChangeTime = FPlatformTime::Seconds();
		}
		break;
	case EShadePhase::Playing:
		if (AShadeCharacter* S = GetShade())
		{
			if (S->GetExposure() > 0.3f && !HasEvent(TEXT("FirstBurn")))
			{
				NotifyEvent(TEXT("FirstBurn"), S);
			}
		}
		break;
	case EShadePhase::Finale:
		TickFinale(Dt);
		break;
	case EShadePhase::Reunion:
		TickReunion(Dt);
		break;
	case EShadePhase::Ended:
		if (IsAutoplayExit() && PhaseTime > 7.f)
		{
			QuitGame();
		}
		break;
	}

	if (bSunMapPending)
	{
		SunMapTimer -= Dt;
		if (SunMapTimer <= 0.f)
		{
			bSunMapPending = false;
			WriteSunMap(TEXT(""));
			if (bHasSunEnd && Sun.IsValid())
			{
				Sun->SetActorRotation(SunEnd);
				WriteSunMap(TEXT("_finale"));
				Sun->SetActorRotation(SunStart);
			}
		}
	}

	if (IsAutoplay() && HasParam(TEXT("autoplayshots")))
	{
		ShotTimer -= Dt;
		if (ShotTimer <= 0.f)
		{
			ShotTimer = 2.5f;
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Shots/auto_%03d.png"), ShotIndex++), true, false);
		}
	}
	if (!MotionCap.IsEmpty())
	{
		TickMotionCap(Dt);
	}
	if (IsAutoplayExit() && RunTime > 420.f)
	{
		UE_LOG(LogShade, Warning, TEXT("SHADE_TIMEOUT autoplay run exceeded 420 s"));
		QuitGame();
	}
}

void AShadeGameMode::TickTitle(float Dt)
{
	if (!TitleCamera)
	{
		return;
	}
	// Slow crane drift over the rooftops.
	const float T = PhaseTime;
	FTransform X = TitleCameraBase;
	const FVector Right = TitleCameraBase.GetRotation().GetRightVector();
	X.AddToTranslation(Right * FMath::Sin(T * 0.11f) * 220.f + FVector(0.f, 0.f, FMath::Sin(T * 0.07f) * 60.f));
	X.SetRotation((TitleCameraBase.Rotator() + FRotator(FMath::Sin(T * 0.09f) * 1.2f, FMath::Sin(T * 0.11f) * -2.5f, 0.f)).Quaternion());
	TitleCamera->SetActorTransform(X);
}

void AShadeGameMode::TickFinale(float Dt)
{
	FinaleTime += Dt;
	// Bells: three tolls, the first right away.
	const float BellAt[3] = { 0.3f, 2.6f, 4.9f };
	while (BellsRung < 3 && FinaleTime >= BellAt[BellsRung])
	{
		ShadeAssets::Play2D(this, TEXT("SFX_BellToll"), 0.75f);
		++BellsRung;
		AddTrauma(0.08f);
	}
	// The sun sinks and swings west: every shadow in the village slides, the bell tower's sweeps over Biscotto.
	const float SunDelay = 1.5f, SunDuration = 9.f;
	FinaleAlpha = ShadeEase::InOutCubic((FinaleTime - SunDelay) / SunDuration);
	if (Sun.IsValid() && bHasSunEnd)
	{
		Sun->SetActorRotation(FQuat::Slerp(SunStart.Quaternion(), SunEnd.Quaternion(), FinaleAlpha));
	}
	if (FinaleTime > SunDelay + SunDuration && !HasEvent(TEXT("TowerShadeArrived")))
	{
		MusicTarget = 0.45f;
		NotifyEvent(TEXT("TowerShadeArrived"), this);
	}
}

void AShadeGameMode::BeginReunion(AShadeNapCat* Cat)
{
	if (Phase == EShadePhase::Reunion || !Cat)
	{
		return;
	}
	Biscotto = Cat;
	SetPhase(EShadePhase::Reunion);
	ReunionTime = 0.f;
	ReunionStep = 0;
	MusicTarget = 0.f;
	ShadeAssets::Play2D(this, TEXT("MUS_Reunion"), 0.85f);

	// Where the cat's shadow belongs: opposite the sun.
	FVector ToSun = FVector(0, 0, 1);
	if (UShadeSunSubsystem* SS = UShadeSunSubsystem::Get(this))
	{
		ToSun = SS->GetToSun();
	}
	const FVector Away = FVector(-ToSun.X, -ToSun.Y, 0.f).GetSafeNormal();
	ReunionSpot = Cat->GetActorLocation() + Away * 32.f;

	AShadeCharacter* S = GetShade();
	if (S)
	{
		S->SetInvulnerable(true);
		S->BeginMerge(ReunionSpot, 2.2f);
	}

	// Close shot from the sun side, cat height: Biscotto lit warm against the church, his shadow pouring away from us.
	const FVector CatLoc = Cat->GetActorLocation();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Away).GetSafeNormal();
	const FVector CamLoc = CatLoc - Away * 185.f + Side * 95.f + FVector(0.f, 0.f, 48.f);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CineCamera = GetWorld()->SpawnActor<ACameraActor>(CamLoc, ((CatLoc + Away * 25.f + FVector(0, 0, 12.f)) - CamLoc).Rotation(), P);
	if (CineCamera)
	{
		CineCamera->GetCameraComponent()->SetFieldOfView(42.f);
		// Portrait focus on the cats, the church soft behind them.
		{
			UCameraComponent* CC = CineCamera->GetCameraComponent();
			CC->PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
			CC->PostProcessSettings.DepthOfFieldFocalDistance = FVector::Dist(CamLoc, CatLoc);
			CC->PostProcessSettings.bOverride_DepthOfFieldFstop = true;
			CC->PostProcessSettings.DepthOfFieldFstop = 2.8f;
			CC->PostProcessBlendWeight = 1.f;
		}
		// Cinematic key + rim so Biscotto reads warm even inside the tower's shade.
		auto AddCineLight = [this](const FVector& Loc, float Lumens, float Radius, const FLinearColor& Col)
		{
			APointLight* L = GetWorld()->SpawnActor<APointLight>(Loc, FRotator::ZeroRotator);
			if (L)
			{
				UPointLightComponent* C = Cast<UPointLightComponent>(L->GetLightComponent());
				C->SetMobility(EComponentMobility::Movable);
				C->SetIntensityUnits(ELightUnits::Lumens);
				C->SetIntensity(Lumens);
				C->SetAttenuationRadius(Radius);
				C->SetLightColor(Col);
				C->SetCastShadows(false);
				C->SetSourceRadius(20.f);
			}
		};
		AddCineLight(CatLoc - Away * 140.f + Side * 70.f + FVector(0, 0, 110.f), 260.f, 420.f, FLinearColor(1.f, 0.74f, 0.5f));
		AddCineLight(CatLoc + Away * 80.f - Side * 50.f + FVector(0, 0, 70.f), 140.f, 260.f, FLinearColor(1.f, 0.62f, 0.32f));
		CineCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			PC->SetViewTargetWithBlend(CineCamera, 1.6f, VTBlend_EaseInOut, 2.f);
		}
	}
}

void AShadeGameMode::TickReunion(float Dt)
{
	ReunionTime += Dt;
	AShadeNapCat* Cat = Biscotto.Get();
	if (CineCamera)
	{
		// Gentle push-in.
		CineCamera->AddActorWorldOffset(CineCamera->GetActorForwardVector() * 6.f * Dt);
	}
	if (ReunionStep == 0 && ReunionTime > 2.25f)
	{
		ReunionStep = 1;
		if (Cat)
		{
			Cat->SetHasShadow(true);
			Cat->SetPurrVolume(0.7f);
		}
		if (AShadeCharacter* S = GetShade())
		{
			S->SpawnBurst(ReunionSpot + FVector(0, 0, 6), 14, 0.f, -60.f);
		}
		ShadeAssets::Play2D(this, TEXT("SFX_Solve"), 0.5f, 0.8f);
		NotifyEvent(TEXT("Reunited"), this);
	}
	if (ReunionStep == 1 && ReunionTime > 3.0f)
	{
		ReunionStep = 2;
		if (Cat)
		{
			Cat->WakeUp();
		}
	}
	if (ReunionStep == 2 && ReunionTime > 5.2f)
	{
		ReunionStep = 3;
		Say(LOCTEXT("Reunion", "There you are. Welcome home, little shadow."), 4.5f);
	}
	if (ReunionStep == 3 && ReunionTime > 7.0f)
	{
		ReunionStep = 4;
		FadeTo(1.f, 0.7f, FLinearColor(0.02f, 0.015f, 0.03f));
	}
	if (ReunionStep == 4 && ReunionTime > 7.8f)
	{
		// Cut (a long blend would fly the camera through the houses) to the wide sunset shot, then fade up.
		ReunionStep = 5;
		for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(TEXT("EndCam")))
			{
				if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
				{
					PC->SetViewTarget(*It);
				}
				break;
			}
		}
		FadeTo(0.f, 1.4f, FLinearColor(0.02f, 0.015f, 0.03f));
	}
	if (ReunionStep == 5 && ReunionTime > 10.5f)
	{
		ReunionStep = 6;
		MusicTarget = 0.4f;
		SetPhase(EShadePhase::Ended);
		UE_LOG(LogShade, Log, TEXT("SHADE_COMPLETE t=%.1f evaporations=%d utc=%s"), RunTime, GetShade() ? GetShade()->GetEvaporations() : -1, *FDateTime::UtcNow().ToIso8601());
	}
}

// =====================================================================================================================
// QA helpers
// =====================================================================================================================

void AShadeGameMode::WriteSunMap(const TCHAR* Suffix)
{
	UShadeSunSubsystem* SS = UShadeSunSubsystem::Get(this);
	if (!SS)
	{
		return;
	}
	FBox Bounds(ForceInit);
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		for (const FName& T : It->Tags)
		{
			if (T.ToString().StartsWith(TEXT("AP_")))
			{
				Bounds += It->GetActorLocation();
			}
		}
	}
	if (!Bounds.IsValid)
	{
		return;
	}
	const FBox2D Area(FVector2D(Bounds.Min) - FVector2D(700.f), FVector2D(Bounds.Max) + FVector2D(700.f));
	SS->WriteSunMap(Area, 10.f, Bounds.Max.Z + 1500.f, FPaths::ProjectSavedDir() / FString::Printf(TEXT("SunMap%s.png"), Suffix));
}

void AShadeGameMode::TickMotionCap(float Dt)
{
	AShadeCharacter* S = GetShade();
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!S || !PC)
	{
		return;
	}
	const bool bTitle = MotionCap == TEXT("title");
	if (!bTitle && Phase != EShadePhase::Playing)
	{
		return;
	}
	static const TCHAR* Bones[] = {
		TEXT("pelvis"), TEXT("spine_03"), TEXT("head"),
		TEXT("tail_01"), TEXT("tail_02"), TEXT("tail_03"), TEXT("tail_04"), TEXT("tail_05"), TEXT("tail_06"), TEXT("tail_07"), TEXT("tail_08"),
		TEXT("upperarm_l"), TEXT("forearm_l"), TEXT("hand_l"), TEXT("finger_l"), TEXT("upperarm_r"), TEXT("forearm_r"), TEXT("hand_r"), TEXT("finger_r"),
		TEXT("thigh_l"), TEXT("calf_l"), TEXT("foot_l"), TEXT("toe_l"), TEXT("thigh_r"), TEXT("calf_r"), TEXT("foot_r"), TEXT("toe_r"),
	};
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("MotionCap") / MotionCap;
	if (!bCapInit)
	{
		bCapInit = true;
		CapStart = S->GetActorLocation();
		CapFwd = S->GetActorForwardVector().GetSafeNormal2D();
		CapCamFocus = CapStart;
		CapCsv = TEXT("frame,t,phase,x,y,z,yaw,speed,camx,camy,camz,campitch,camyaw,camroll,arm");
		for (const TCHAR* B : Bones)
		{
			CapCsv += FString::Printf(TEXT(",%s_px,%s_py,%s_pz,%s_qx,%s_qy,%s_qz,%s_qw"), B, B, B, B, B, B, B);
		}
		CapCsv += TEXT("\n");
		S->SetInvulnerable(true);
		S->SetControlEnabled(true);
		if (MotionCap == TEXT("side"))
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CapCam = GetWorld()->SpawnActor<ACameraActor>(CapStart, FRotator::ZeroRotator, P);
			if (CapCam)
			{
				CapCam->GetCameraComponent()->SetFieldOfView(50.f);
				CapCam->GetCameraComponent()->bConstrainAspectRatio = false;
				PC->SetViewTarget(CapCam);
			}
		}
		UE_LOG(LogShade, Log, TEXT("SHADE_MOTIONCAP %s start %s fwd %s"), *MotionCap, *CapStart.ToCompactString(), *CapFwd.ToCompactString());
	}
	const float T0 = CapTime;
	CapTime += Dt;
	const float T = CapTime;
	auto Crossed = [T0, T](float At) { return T0 < At && T >= At; };
	const FVector Up = FVector::UpVector;
	const FVector Right = FVector::CrossProduct(Up, CapFwd);

	float EndAt = 16.5f;
	if (bTitle)
	{
		// Title crane drift, then "Begin" and the swoop down into the passage.
		EndAt = 13.f;
		if (Crossed(6.5f))
		{
			StartGame();
		}
	}
	else
	{
		// Idle, walk, veer, stop, about-turn, trot, stop, walking hop - the moves a player makes in the first minute.
		FVector Wish = FVector::ZeroVector;
		bool bSprintNow = false;
		if (T >= 1.f && T < 4.5f) { Wish = CapFwd; }
		else if (T >= 4.5f && T < 6.f) { Wish = CapFwd.RotateAngleAxis(-40.f * FMath::Sin(PI * (T - 4.5f) / 1.5f), Up); }
		else if (T >= 7.6f && T < 8.8f) { Wish = -CapFwd; }
		else if (T >= 10.f && T < 11.8f) { Wish = CapFwd; bSprintNow = true; }
		else if (T >= 13.f && T < 14.6f) { Wish = CapFwd; }
		if (Crossed(13.5f))
		{
			S->JumpInput();
		}
		S->SetSprint(bSprintNow);
		if (!Wish.IsNearlyZero())
		{
			S->AddMovementInput(Wish, 1.f);
		}
		if (CapCam)
		{
			// Three-quarter side view at cat height, following a smoothed focus point.
			const FVector Focus = S->GetActorLocation() - FVector(0.f, 0.f, 8.f);
			CapCamFocus = FMath::Lerp(CapCamFocus, Focus, 1.f - FMath::Exp(-10.f * Dt));
			const FVector CamLoc = CapCamFocus + Right * 150.f - CapFwd * 35.f + Up * 22.f;
			CapCam->SetActorLocationAndRotation(CamLoc, (CapCamFocus - CamLoc).Rotation());
		}
	}

	// Frame + tracks.
	FScreenshotRequest::RequestScreenshot(Dir / FString::Printf(TEXT("f_%04d.png"), CapFrame), false, false);
	const FVector L = S->GetActorLocation();
	const APlayerCameraManager* PCM = PC->PlayerCameraManager;
	const FVector CL = PCM ? PCM->GetCameraLocation() : FVector::ZeroVector;
	const FRotator CR = PCM ? PCM->GetCameraRotation() : FRotator::ZeroRotator;
	const float Arm = S->GetBoom() ? FVector::Dist(S->GetBoom()->GetComponentLocation(), S->GetCamera()->GetComponentLocation()) : 0.f;
	CapCsv += FString::Printf(TEXT("%d,%.4f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.3f"), CapFrame, T, static_cast<int32>(Phase),
		L.X, L.Y, L.Z, S->GetActorRotation().Yaw, S->GetVelocity().Size2D(), CL.X, CL.Y, CL.Z, CR.Pitch, CR.Yaw, CR.Roll, Arm);
	UPoseableMeshComponent* Body = S->GetBody();
	for (const TCHAR* B : Bones)
	{
		const int32 Bi = Body ? Body->GetBoneIndex(FName(B)) : INDEX_NONE;
		const FTransform X = Bi != INDEX_NONE ? Body->GetBoneTransform(Bi) : FTransform::Identity;
		const FVector P = X.GetLocation();
		const FQuat Q = X.GetRotation();
		CapCsv += FString::Printf(TEXT(",%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.5f"), P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W);
	}
	CapCsv += TEXT("\n");
	++CapFrame;
	if (T >= EndAt)
	{
		FFileHelper::SaveStringToFile(CapCsv, *(Dir / TEXT("tracks.csv")));
		UE_LOG(LogShade, Log, TEXT("SHADE_MOTIONCAP done %d frames"), CapFrame);
		MotionCap.Empty();
		QuitGame();
	}
}

void AShadeGameMode::RunCamShots()
{
	// QA: one UI-free screenshot per camera tagged CamCandidate_<n> (placed by sns_lighting.py), then quit.
	TArray<ACameraActor*> Cams;
	for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
	{
		for (const FName& T : It->Tags)
		{
			if (T.ToString().StartsWith(TEXT("CamCandidate")))
			{
				Cams.Add(*It);
			}
		}
	}
	Cams.Sort([](const ACameraActor& A, const ACameraActor& B) { return A.Tags[0].LexicalLess(B.Tags[0]); });
	TWeakObjectPtr<AShadeGameMode> Self(this);
	const float Step = 2.6f;
	for (int32 i = 0; i < Cams.Num(); ++i)
	{
		TWeakObjectPtr<ACameraActor> C(Cams[i]);
		const FString Name = Cams[i]->Tags[0].ToString();
		FTimerHandle H0, H1;
		GetWorldTimerManager().SetTimer(H0, [Self, C]()
		{
			APlayerController* PC = Self.IsValid() ? UGameplayStatics::GetPlayerController(Self.Get(), 0) : nullptr;
			if (PC && C.IsValid()) { PC->SetViewTarget(C.Get()); }
		}, 1.0f + i * Step, false);
		GetWorldTimerManager().SetTimer(H1, [Name]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Shots/%s.png"), *Name), false, false);
		}, 1.0f + i * Step + 2.2f, false);
	}
	FTimerHandle HQ;
	GetWorldTimerManager().SetTimer(HQ, [Self]() { if (Self.IsValid()) { Self->QuitGame(); } }, 2.0f + Cams.Num() * Step + 0.6f, false);
	UE_LOG(LogShade, Log, TEXT("SHADE_CAMSHOTS %d cameras"), Cams.Num());
}

void AShadeGameMode::RunCatGallery()
{
	// Lines up the cat poses in front of a camera, screenshots, quits. Used to tune ShadeCatRig without the editor.
	FVector Origin(0.f, 0.f, 0.f);
	FRotator Facing(0.f, 0.f, 0.f);
	for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("Gallery")))
		{
			Origin = It->GetActorLocation();
			Facing = It->GetActorRotation();
		}
	}
	const ECatPose Poses[5] = { ECatPose::Sleep, ECatPose::Sit, ECatPose::Locomote, ECatPose::Locomote, ECatPose::Locomote };
	const float Speeds[5] = { 0.f, 0.f, 135.f, 300.f, 0.f };
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 i = 0; i < 5; ++i)
	{
		const FVector Fwd = FRotationMatrix(Facing).GetUnitAxis(EAxis::X);
		AShadeNapCat* C = GetWorld()->SpawnActor<AShadeNapCat>(Origin + Fwd * (i - 2) * 78.f, Facing, P);
		if (C)
		{
			C->GetRig()->SetPose(Poses[i], 0.01f);
			C->GetRig()->TreadmillSpeed = Speeds[i];
			C->GetRig()->bGrounded = true;
			// NapCat starts asleep; override after its BeginPlay ran.
			FTimerHandle H;
			TWeakObjectPtr<AShadeNapCat> WC(C);
			const ECatPose Pose = Poses[i];
			const float Sp = Speeds[i];
			GetWorldTimerManager().SetTimer(H, [WC, Pose, Sp]() { if (WC.IsValid()) { WC->GetRig()->SetPose(Pose, 0.01f); WC->GetRig()->TreadmillSpeed = Sp; } }, 0.2f, false);
		}
	}
	ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(Origin + Right * 300.f + FVector(0, 0, 55.f), FRotator::ZeroRotator, P);
	if (Cam)
	{
		Cam->SetActorRotation(((Origin + FVector(0, 0, 20.f)) - Cam->GetActorLocation()).Rotation());
		Cam->GetCameraComponent()->SetFieldOfView(62.f);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			FTimerHandle H0;
			TWeakObjectPtr<APlayerController> WPC(PC);
			TWeakObjectPtr<ACameraActor> WCam(Cam);
			GetWorldTimerManager().SetTimer(H0, [WPC, WCam]() { if (WPC.IsValid() && WCam.IsValid()) { WPC->SetViewTarget(WCam.Get()); } }, 0.3f, false);
		}
	}
	FTimerHandle H1, H2, H3;
	TWeakObjectPtr<UWorld> WW(GetWorld());
	GetWorldTimerManager().SetTimer(H3, [WW]()
	{
		for (TActorIterator<AShadeNapCat> It(WW.Get()); It; ++It)
		{
			UPoseableMeshComponent* M = It->Mesh;
			const FBoxSphereBounds B = M->Bounds;
			UE_LOG(LogShade, Log, TEXT("SHADE_GALLERY %s pose %d visible %d bounds o=%s e=%s head=%s pelvis=%s"), *It->GetName(),
				static_cast<int32>(It->GetRig()->GetPose()), M->IsVisible() ? 1 : 0, *B.Origin.ToCompactString(), *B.BoxExtent.ToCompactString(),
				*M->GetBoneLocation(TEXT("head")).ToCompactString(), *M->GetBoneLocation(TEXT("pelvis")).ToCompactString());
		}
	}, 1.5f, false);
	GetWorldTimerManager().SetTimer(H1, []() { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots/cat_gallery.png"), false, false); }, 2.5f, false);
	// Second angle: three-quarter view from the front, closer.
	FTimerHandle H4, H5, H6;
	TWeakObjectPtr<ACameraActor> WCam2(Cam);
	const FVector Fwd0 = FRotationMatrix(Facing).GetUnitAxis(EAxis::X);
	GetWorldTimerManager().SetTimer(H4, [WCam2, Origin, Fwd0, Right]()
	{
		if (WCam2.IsValid())
		{
			WCam2->SetActorLocation(Origin + Fwd0 * 230.f + Right * 150.f + FVector(0, 0, 70.f));
			WCam2->SetActorRotation(((Origin + Fwd0 * 40.f + FVector(0, 0, 18.f)) - WCam2->GetActorLocation()).Rotation());
		}
	}, 2.9f, false);
	GetWorldTimerManager().SetTimer(H5, []() { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots/cat_gallery_34.png"), false, false); }, 3.6f, false);
	GetWorldTimerManager().SetTimer(H6, [WCam2, Origin, Fwd0, Right]()
	{
		if (WCam2.IsValid())
		{
			WCam2->SetActorLocation(Origin + Fwd0 * 222.f + Right * 48.f + FVector(0, 0, 32.f));
			WCam2->SetActorRotation(((Origin + Fwd0 * 168.f + FVector(0, 0, 25.f)) - WCam2->GetActorLocation()).Rotation());
			WCam2->GetCameraComponent()->SetFieldOfView(40.f);
		}
	}, 4.0f, false);
	FTimerHandle H7;
	GetWorldTimerManager().SetTimer(H7, []() { FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots/cat_gallery_face.png"), false, false); }, 4.7f, false);
	// Gait filmstrip: the walking and trotting cats from the side, 16 frames ~70 ms apart.
	FTimerHandle H8;
	GetWorldTimerManager().SetTimer(H8, [WCam2, Origin, Fwd0, Right]()
	{
		if (WCam2.IsValid())
		{
			WCam2->SetActorLocation(Origin + Fwd0 * 39.f + Right * 175.f + FVector(0, 0, 28.f));
			WCam2->SetActorRotation(((Origin + Fwd0 * 39.f + FVector(0, 0, 18.f)) - WCam2->GetActorLocation()).Rotation());
			WCam2->GetCameraComponent()->SetFieldOfView(44.f);
		}
	}, 5.0f, false);
	for (int32 k = 0; k < 16; ++k)
	{
		FTimerHandle HK;
		GetWorldTimerManager().SetTimer(HK, [k]()
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Shots/gait_%02d.png"), k), false, false);
		}, 5.6f + k * 0.07f, false);
	}
	GetWorldTimerManager().SetTimer(H2, [this]() { QuitGame(); }, 7.4f, false);
}

#undef LOCTEXT_NAMESPACE
