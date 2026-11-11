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
#include "Sound/ReverbEffect.h"
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
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Scalability.h"
#include "HAL/IConsoleManager.h"
#include "AudioMixerBlueprintLibrary.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "Containers/Ticker.h"

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

void AShadeGameMode::ApplyRenderBudget()
{
	if (HasParam(TEXT("epic")))
	{
		UE_LOG(LogShade, Log, TEXT("SHADE_RENDER epic (no budget preset)"));
		return;
	}
	// Measured on the target laptop (Saved/Perf, Scripts/perf_run.ps1): Epic at native 1080p cost ~19 ms of GPU -
	// shadow depths 5.3, Lumen + lighting 3.2, TSR with a 200% history 2.2, volumetric fog 0.9 - and ~20 M classic
	// triangles a frame (props that were meant to be Nanite, see sns_nanite_props.py). Now ~8 ms GPU, ~92 fps median
	// in Development with the CSV profiler running (render thread bound).
	Scalability::FQualityLevels Q = Scalability::GetQualityLevels();
	Q.SetFromSingleQualityLevel(2);
	// Lumen's irradiance-volume final gather (Medium GI): keeps the warm bounce in the shaded lanes (turning Lumen
	// off leaves them cold blue-grey) for ~1.5 ms less render thread and GPU than screen probes.
	Q.GlobalIlluminationQuality = 1;
	Q.ResolutionQuality = 100.f;
	Q.ViewDistanceQuality = 3;
	Q.TextureQuality = 3;
	Q.FoliageQuality = 3;
	Q.ShadingQuality = 3;
	Scalability::SetQualityLevels(Q, true);
	auto Set = [](const TCHAR* Name, const TCHAR* Value)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			V->Set(Value, ECVF_SetByCode);
		}
	};
	// Two cascades cover the lanes (shadow depths 2.5 -> 1.5 ms, ~70% resolution instead of the 58% floor);
	// distance-field shadows take over beyond them.
	Set(TEXT("r.Shadow.CSM.MaxCascades"), TEXT("2"));
	// Nearly everything is Nanite (GPU-culled), so hardware occlusion queries only cost a render-thread sync.
	Set(TEXT("r.AllowOcclusionQueries"), TEXT("0"));
	// Plaster and cobbles are rough: screen-space reflections are enough.
	Set(TEXT("r.Lumen.Reflections.Allow"), TEXT("0"));
	// Dynamic resolution: TSR renders between 58% and 100% of 1080p to hold ~115 fps.
	Set(TEXT("r.DynamicRes.OperationMode"), TEXT("2"));
	Set(TEXT("r.DynamicRes.MinScreenPercentage"), TEXT("58"));
	Set(TEXT("r.DynamicRes.MaxScreenPercentage"), TEXT("100"));
	Set(TEXT("r.DynamicRes.FrameTimeBudget"), TEXT("8.7"));
	UE_LOG(LogShade, Log, TEXT("SHADE_RENDER budget preset applied"));
}

void AShadeGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	ApplyRenderBudget();
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
	Add(TEXT("Zone_AfterWash"), TEXT("OBJ_Wash"), LOCTEXT("O3", "Wait out the sea breeze"), LOCTEXT("O3H", "Hold E on the pulley to run the washing out. Cross while the sheets hang still."));
	Add(TEXT("Zone_AfterShutter"), TEXT("OBJ_Shutter"), LOCTEXT("O4s", "Beat the shutter"), LOCTEXT("O4sH", "Tug the cord, then trot (Shift) through its shade before the breeze swings it shut."));
	Add(TEXT("AzzurraOut"), TEXT("OBJ_Azzurra"), LOCTEXT("O5a", "Shade the garden wall"), LOCTEXT("O5aH", "The terrace is in full sun. Casa Azzurra's awning cord hangs by its corner."));
	Add(TEXT("Zone_Terrace"), TEXT("OBJ_Pot"), LOCTEXT("O5b", "One more shadow"), LOCTEXT("O5bH", "From the shaded wall, nudge the potted lemon tree into the last sunny gap."));
	Add(TEXT("TowerShadeArrived"), TEXT("OBJ_Wait"), LOCTEXT("O4", "Wait for the bell tower's shadow"), LOCTEXT("O4H", "The sun is sinking. Stay in the tower's shade and let it carry you to him."));
	Add(TEXT("Reunited"), TEXT("OBJ_Biscotto"), LOCTEXT("O5", "Reunite with Biscotto"), LOCTEXT("O5H", "Slip into his shadow - press E beside him."));

	EventLines.Add(TEXT("FirstBurn"), LOCTEXT("L_Burn", "Ouch! Sunlight is no place for a shadow."));
	EventLines.Add(TEXT("Evaporated"), LOCTEXT("L_Evap", "Poof... back to the last cool spot."));
	EventLines.Add(TEXT("CartSolved"), LOCTEXT("L_Cart", "A cart full of lemons makes a fine patch of shade."));
	EventLines.Add(TEXT("AwningSolved"), LOCTEXT("L_Awning", "Signora Rosa won't mind. Probably."));
	EventLines.Add(TEXT("PotSolved"), LOCTEXT("L_Pot", "Cats do love knocking things off ledges."));
	EventLines.Add(TEXT("LineOut"), LOCTEXT("L_Line", "Washing day. The sea breeze keeps lifting the sheets..."));
	EventLines.Add(TEXT("Zone_AfterWash"), LOCTEXT("L_Wash", "Timed it like a cat on a windowsill."));
	EventLines.Add(TEXT("Zone_AfterShutter"), LOCTEXT("L_Shutter", "Slam! Not today, breeze."));
	EventLines.Add(TEXT("AzzurraOut"), LOCTEXT("L_Azzurra", "Blue shade for the wall. Now that lemon tree..."));
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
		TMap<FString, int32> Kinds;
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
					const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(C);
					Kinds.FindOrAdd(SMC && SMC->GetStaticMesh() ? SMC->GetStaticMesh()->GetName() : C->GetClass()->GetName())++;
				}
			}
		}
		FString List;
		for (const TPair<FString, int32>& K : Kinds)
		{
			List += FString::Printf(TEXT(" %s:%d"), *K.Key, K.Value);
		}
		UE_LOG(LogShade, Log, TEXT("SHADE_CAMERA %d small props no longer block the camera:%s"), Freed, *List);
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

	ShadeAssets::PreloadAudio();
	// Area reverbs (EAX-style presets): a long stone tail under the vault, a short slap between the lane walls and on
	// the stairs, a light open space on the piazza and the terrace. Only positional sounds send to reverb.
	auto MakeReverb = [this](float Density, float Diffusion, float Gain, float GainHF, float Decay, float DecayHF, float RefGain, float RefDelay,
		float LateGain, float LateDelay)
	{
		UReverbEffect* R = NewObject<UReverbEffect>(this);
		R->Density = Density; R->Diffusion = Diffusion; R->Gain = Gain; R->GainHF = GainHF; R->DecayTime = Decay; R->DecayHFRatio = DecayHF;
		R->ReflectionsGain = RefGain; R->ReflectionsDelay = RefDelay; R->LateGain = LateGain; R->LateDelay = LateDelay; R->AirAbsorptionGainHF = 0.994f;
		Reverbs.Add(R);
	};
	MakeReverb(1.f, 1.f, 0.32f, 0.76f, 2.7f, 0.79f, 0.25f, 0.013f, 1.58f, 0.020f);    // vault: stone corridor
	MakeReverb(1.f, 0.3f, 0.32f, 0.73f, 1.49f, 0.86f, 0.25f, 0.007f, 0.99f, 0.011f);  // lanes / stairs: alley
	MakeReverb(1.f, 0.5f, 0.32f, 0.40f, 1.49f, 0.67f, 0.07f, 0.007f, 0.14f, 0.011f);  // piazza / terrace: open town
	// Master bus limiter: the sounds are loudness-matched offline (prep_audio.py); this only catches stacked peaks
	// (bell tolls over the music, a pot smash during the score) so the mix never clips.
	if (USubmixEffectDynamicsProcessorPreset* Limiter = NewObject<USubmixEffectDynamicsProcessorPreset>(this))
	{
		FSubmixEffectDynamicsProcessorSettings L;
		L.DynamicsProcessorType = ESubmixEffectDynamicsProcessorType::Limiter;
		L.PeakMode = ESubmixEffectDynamicsPeakMode::Peak;
		L.ThresholdDb = -1.5f;
		L.KneeBandwidthDb = 0.f;
		L.LookAheadMsec = 3.f;
		L.AttackTimeMsec = 1.f;
		L.ReleaseTimeMsec = 150.f;
		Limiter->SetSettings(L);
		UAudioMixerBlueprintLibrary::AddMasterSubmixEffect(this, Limiter);
	}
	if (HasParam(TEXT("audiocap")))
	{
		// QA: record the final master mix (after the limiter) to Saved/AudioCap/audiocap.wav until the game quits.
		UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 400.f);
	}
	if (USoundBase* S = ShadeAssets::Sound(TEXT("MUS_Theme")))
	{
		Music = UGameplayStatics::SpawnSound2D(this, S, 0.f, 1.f, 0.f, nullptr, true, false);
	}
	if (USoundBase* S = ShadeAssets::Sound(TEXT("AMB_Sea_Loop")))
	{
		AmbSea = UGameplayStatics::SpawnSound2D(this, S, 0.42f, 1.f, 0.f, nullptr, true, false);
	}
	if (USoundBase* S = ShadeAssets::Sound(TEXT("AMB_Village_Loop")))
	{
		AmbVillage = UGameplayStatics::SpawnSound2D(this, S, 0.55f, 1.f, 0.f, nullptr, true, false);
	}
	if (USoundBase* S = ShadeAssets::Sound(TEXT("AMB_Town_Loop")))
	{
		AmbTown = UGameplayStatics::SpawnSound2D(this, S, 0.f, 1.f, 0.f, nullptr, true, false);
	}
	MusicTarget = 0.65f;

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
	float PauseShotAt = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("pauseshot="), PauseShotAt) && PauseShotAt > 0.f)
	{
		// QA: open the pause menu at that run time, screenshot it, close it (real-time ticker: the world is paused).
		TWeakObjectPtr<AShadeGameMode> Self(this);
		TSharedRef<int32> Stage = MakeShared<int32>(0);
		TSharedRef<double> T0 = MakeShared<double>(0.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Self, Stage, T0, PauseShotAt](float)
		{
			if (!Self.IsValid()) { return false; }
			AShadePlayerController* PC = Cast<AShadePlayerController>(UGameplayStatics::GetPlayerController(Self.Get(), 0));
			const double Now = FPlatformTime::Seconds();
			if (*Stage == 0 && Self->GetRunTime() > PauseShotAt && Self->GetPhase() == EShadePhase::Playing && PC)
			{
				PC->OpenPauseMenu();
				*T0 = Now;
				*Stage = 1;
			}
			else if (*Stage == 1 && Now - *T0 > 0.9)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Shots/ui_pause.png"), true, false);
				*Stage = 2;
			}
			else if (*Stage == 2 && Now - *T0 > 1.8 && PC)
			{
				PC->ClosePauseMenu();
				return false;
			}
			return true;
		}));
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
	WritePerfLog();
	UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0), EQuitPreference::Quit, false);
}

void AShadeGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	WritePerfLog();
	Super::EndPlay(Reason);
}

void AShadeGameMode::WritePerfLog()
{
	if (PerfFrames.Num() < 60)
	{
		return;
	}
	TArray<float> S = PerfFrames;
	S.Sort();
	double Sum = 0.0;
	for (const float F : S)
	{
		Sum += F;
	}
	auto Pct = [&S](float P) { return S[FMath::Clamp(FMath::FloorToInt(P * S.Num()), 0, S.Num() - 1)] * 1000.f; };
	const FString Summary = FString::Printf(TEXT("frames %d  mean fps %.1f  p50 %.2f ms (%.1f fps)  p95 %.2f ms (%.1f fps)  p99 %.2f ms  max %.1f ms\n"),
		S.Num(), S.Num() / Sum, Pct(0.5f), 1000.f / Pct(0.5f), Pct(0.95f), 1000.f / Pct(0.95f), Pct(0.99f), S.Last() * 1000.f);
	FFileHelper::SaveStringToFile(Summary, *(FPaths::ProjectSavedDir() / TEXT("Perf") / TEXT("perflog.txt")), FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(), FILEWRITE_Append);
	UE_LOG(LogShade, Log, TEXT("SHADE_PERF %s"), *Summary);
	PerfFrames.Reset();
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
	if (EventName == TEXT("CartSolved") || EventName == TEXT("AwningSolved") || EventName == TEXT("Zone_AfterWash") || EventName == TEXT("Zone_AfterShutter")
		|| EventName == TEXT("PotSolved"))
	{
		if (bNew)
		{
			++PuzzlesSolved;
			ShadeAssets::Play2D(this, TEXT("SFX_Solve"), 0.6f);
			PushToast(FText::Format(LOCTEXT("Solved", "{0} of {1}"), FText::AsNumber(PuzzlesSolved), FText::AsNumber(PuzzleCount)));
		}
	}
	if (EventName == TEXT("Zone_Terrace") && bNew && Phase == EShadePhase::Playing)
	{
		SetPhase(EShadePhase::Finale);
		FinaleTime = 0.f;
		BellsRung = 0;
		MusicTarget = 0.36f;
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
	static const bool bPerfLog = HasParam(TEXT("perflog"));
	if (bPerfLog)
	{
		const double Now = FPlatformTime::Seconds();
		if (PerfLast > 0.0 && (Phase == EShadePhase::Playing || Phase == EShadePhase::Finale) && PhaseTime > 2.f)
		{
			PerfFrames.Add(static_cast<float>(Now - PerfLast));
		}
		PerfLast = Now;
	}
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

	// Town bed follows Shade: lane and piazza (x < ~2450) full, gone by the top of the stairs.
	if (AmbTown)
	{
		const AShadeCharacter* S = GetShade();
		const float X = S ? S->GetActorLocation().X : 0.f;
		const float Want = (Phase == EShadePhase::Title ? 0.32f : 0.5f) * (1.f - ShadeEase::Smooth((X - 2450.f) / 700.f));
		TownLevel = ShadeEase::Approach(TownLevel, Want, 1.5f, Dt);
		AmbTown->SetVolumeMultiplier(TownLevel);
	}

	// Area reverb follows Shade (title / end: the open town).
	if (Reverbs.Num() == 3)
	{
		const AShadeCharacter* S = GetShade();
		const float X = (S && Phase != EShadePhase::Title) ? S->GetActorLocation().X : 2000.f;
		const int32 Area = X < -150.f ? 0 : (X < 1700.f ? 1 : (X < 2500.f ? 2 : (X < 3330.f ? 1 : 2)));
		if (Area != ReverbArea)
		{
			ReverbArea = Area;
			UGameplayStatics::ActivateReverbEffect(this, Reverbs[Area], TEXT("ShadeArea"), 0.f, Area == 2 ? 0.45f : 0.7f, 0.8f);
		}
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
		if (PhaseTime > 2.f && !bAudioCapDone && HasParam(TEXT("audiocap")))
		{
			// The WAV is written asynchronously: stop well before the autoplay quits.
			bAudioCapDone = true;
			const FString CapDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AudioCap"));
			IFileManager::Get().MakeDirectory(*CapDir, true);
			UAudioMixerBlueprintLibrary::StopRecordingOutput(this, EAudioRecordingExportType::WavFile, TEXT("audiocap"), CapDir);
		}
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
	const float BellAt[3] = { 0.3f, 2.1f, 3.9f };
	while (BellsRung < 3 && FinaleTime >= BellAt[BellsRung])
	{
		ShadeAssets::Play2D(this, TEXT("SFX_BellToll"), 0.75f);
		++BellsRung;
		AddTrauma(0.08f);
	}
	// The sun sinks and swings west: every shadow in the village slides, the bell tower's sweeps over Biscotto.
	const float SunDelay = 0.9f, SunDuration = 6.8f;
	FinaleAlpha = ShadeEase::InOutCubic((FinaleTime - SunDelay) / SunDuration);
	if (Sun.IsValid() && bHasSunEnd)
	{
		Sun->SetActorRotation(FQuat::Slerp(SunStart.Quaternion(), SunEnd.Quaternion(), FinaleAlpha));
	}
	if (FinaleTime > SunDelay + SunDuration && !HasEvent(TEXT("TowerShadeArrived")))
	{
		MusicTarget = 0.58f;
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
	ShadeAssets::Play2D(this, TEXT("MUS_Reunion"), 1.f);

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
			// The tower's shade is dim, so auto exposure opened right up and the ginger coat blew out: hold it down a
			// little so the coat keeps its colour and the evening light stays evening.
			CC->PostProcessSettings.bOverride_AutoExposureBias = true;
			CC->PostProcessSettings.AutoExposureBias = 0.15f;
			CC->PostProcessSettings.bOverride_AutoExposureMinBrightness = true;  // don't open up as far in the dark
			CC->PostProcessSettings.AutoExposureMinBrightness = 1.25f;
			CC->PostProcessBlendWeight = 1.f;
		}
		// Soft cinematic key + rim so Biscotto reads warm inside the tower's shade without glowing (a gentle fill, not a
		// spotlight).
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
		AddCineLight(CatLoc - Away * 140.f + Side * 70.f + FVector(0, 0, 110.f), 85.f, 380.f, FLinearColor(1.f, 0.78f, 0.6f));
		AddCineLight(CatLoc + Away * 80.f - Side * 50.f + FVector(0, 0, 70.f), 55.f, 240.f, FLinearColor(1.f, 0.66f, 0.4f));
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
	if (ReunionStep == 0 && ReunionTime > 2.1f)
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
	if (ReunionStep == 1 && ReunionTime > 2.8f)
	{
		ReunionStep = 2;
		if (Cat)
		{
			Cat->WakeUp();
		}
	}
	if (ReunionStep == 2 && ReunionTime > 4.6f)
	{
		ReunionStep = 3;
		Say(LOCTEXT("Reunion", "There you are. Welcome home, little shadow."), 4.5f);
	}
	if (ReunionStep == 3 && ReunionTime > 7.2f)  // Biscotto is up and sitting by ~6.3 s: hold on him a moment
	{
		ReunionStep = 4;
		FadeTo(1.f, 0.7f, FLinearColor(0.02f, 0.015f, 0.03f));
	}
	if (ReunionStep == 4 && ReunionTime > 7.9f)
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
	if (ReunionStep == 5 && ReunionTime > 10.2f)
	{
		ReunionStep = 6;
		MusicTarget = 0.52f;
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
	if (!bTitle && Phase != EShadePhase::Playing && !(MotionCap == TEXT("tour") && Phase == EShadePhase::Finale))
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
		CapYaw = PC->GetControlRotation().Yaw;
		CapCsv = TEXT("frame,t,phase,x,y,z,yaw,speed,camx,camy,camz,campitch,camyaw,camroll,arm,vis,nan,los,evap,exposure,falling");
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
	else if (MotionCap == TEXT("still"))
	{
		// No input at all: anything that changes between frames is the renderer (shimmer, noise, flicker).
		// -capend=<s> sets the length, -capcmds="1.5=r.Foo 0;3=r.Bar 1" runs console commands at set times so one
		// capture can A/B several render features.
		EndAt = 3.f;
		FString EndStr;
		if (FParse::Value(FCommandLine::Get(), TEXT("capend="), EndStr))
		{
			EndAt = FCString::Atof(*EndStr);
		}
		// -capdrift=<deg>: a slow look-around pan, because temporal artefacts only show while the view moves.
		float Drift = 0.f;
		if (FParse::Value(FCommandLine::Get(), TEXT("capdrift="), Drift) && Drift > 0.f)
		{
			PC->SetControlRotation(FRotator(-12.f, CapYaw + Drift * FMath::Sin(2.f * PI * 0.3f * T), 0.f));
		}
		FString Cmds;
		if (FParse::Value(FCommandLine::Get(), TEXT("capcmds="), Cmds, false))
		{
			TArray<FString> Items;
			Cmds.TrimQuotes().ParseIntoArray(Items, TEXT(";"));
			for (const FString& Item : Items)
			{
				FString At, Cmd;
				if (Item.Split(TEXT("="), &At, &Cmd) && Crossed(FCString::Atof(*At)))
				{
					PC->ConsoleCommand(Cmd, true);
					UE_LOG(LogShade, Log, TEXT("SHADE_CAPCMD frame %d: %s"), CapFrame, *Cmd);
				}
			}
		}
	}
	else if (MotionCap == TEXT("tour"))
	{
		// Hold at the start and every autoplay waypoint for 2.2 s with a slow pan: finds z-fighting and shimmer
		// anywhere on the route (both only show while the view moves).
		static const TCHAR* Stops[] = { TEXT("AP_Lane1"), TEXT("AP_SunBack"), TEXT("AP_CartGrab"), TEXT("AP_Alley1"), TEXT("AP_Alley2"),
			TEXT("AP_Alley3"), TEXT("AP_Cord"), TEXT("AP_Piazza1"), TEXT("AP_Piazza2"), TEXT("AP_Steps1"), TEXT("AP_WallJump"),
			TEXT("AP_Steps2"), TEXT("AP_Steps3"), TEXT("AP_Terrace"), TEXT("AP_Porch"), TEXT("AP_Biscotto") };
		const int32 N = UE_ARRAY_COUNT(Stops);
		const float Hold = 2.2f;
		EndAt = Hold * (N + 1);
		auto FindPoint = [this](const TCHAR* Tag) -> AActor*
		{
			for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(Tag))
				{
					return *It;
				}
			}
			return nullptr;
		};
		const int32 Idx = FMath::FloorToInt(T / Hold);
		if (Idx != FMath::FloorToInt(T0 / Hold) && Idx >= 1 && Idx <= N)
		{
			if (const AActor* P = FindPoint(Stops[Idx - 1]))
			{
				const AActor* Next = Idx < N ? FindPoint(Stops[Idx]) : nullptr;
				const FVector Loc = P->GetActorLocation();
				const FVector Heading = Next ? (Next->GetActorLocation() - Loc).GetSafeNormal2D() : CapFwd;
				S->TeleportTo(Loc + FVector(0.f, 0.f, 30.f), Heading.Rotation());
				CapYaw = Heading.Rotation().Yaw;
				UE_LOG(LogShade, Log, TEXT("SHADE_TOUR stop %d %s frame %d at %s"), Idx, Stops[Idx - 1], CapFrame, *Loc.ToCompactString());
			}
		}
		S->GetBoom()->bEnableCameraLag = false;
		S->GetBoom()->bEnableCameraRotationLag = false;
		PC->SetControlRotation(FRotator(-12.f, CapYaw + 3.f * FMath::Sin(2.f * PI * 0.3f * T), 0.f));
	}
	else if (MotionCap == TEXT("jumps"))
	{
		// Hop in place under the vault, while walking, while trotting, then with the camera swung toward a wall.
		EndAt = 13.f;
		FVector Wish = FVector::ZeroVector;
		bool bSprintNow = false;
		if (T >= 2.4f && T < 4.8f) { Wish = CapFwd; }
		else if (T >= 5.6f && T < 7.8f) { Wish = CapFwd; bSprintNow = true; }
		else if (T >= 8.6f && T < 9.6f) { Wish = -CapFwd; }
		if (Crossed(1.f) || Crossed(3.4f) || Crossed(6.6f) || Crossed(11.f))
		{
			S->JumpInput();
		}
		S->SetSprint(bSprintNow);
		if (!Wish.IsNearlyZero())
		{
			S->AddMovementInput(Wish, 1.f);
		}
		const float Swing = FMath::SmoothStep(9.8f, 10.8f, T);
		PC->SetControlRotation(FRotator(-12.f - 10.f * Swing, CapYaw + 75.f * Swing, 0.f));
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
	UPoseableMeshComponent* Body = S->GetBody();
	// Is the cat actually on screen? Body visibility, any non-finite bone, and how much of the camera -> chest line is
	// clear (1 = nothing in the way).
	bool bNan = false;
	for (int32 Bi = 0; Body && Bi < Body->GetNumBones(); ++Bi)
	{
		const FTransform X = Body->GetBoneTransform(Bi);
		bNan |= X.ContainsNaN();
	}
	float Los = 1.f;
	{
		const FVector Chest = L + FVector(0.f, 0.f, 6.f);
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(ShadeCapLos), true, S);
		if (GetWorld()->LineTraceSingleByChannel(Hit, CL, Chest, ECC_Visibility, Q))
		{
			Los = Hit.Time;
		}
	}
	CapCsv += FString::Printf(TEXT("%d,%.4f,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.3f,%d,%d,%.3f,%d,%.3f,%d"), CapFrame, T, static_cast<int32>(Phase),
		L.X, L.Y, L.Z, S->GetActorRotation().Yaw, S->GetVelocity().Size2D(), CL.X, CL.Y, CL.Z, CR.Pitch, CR.Yaw, CR.Roll, Arm,
		Body && Body->IsVisible() ? 1 : 0, bNan ? 1 : 0, Los, S->IsEvaporating() ? 1 : 0, S->GetExposure(), S->GetCharacterMovement()->IsFalling() ? 1 : 0);
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
