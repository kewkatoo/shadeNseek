#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShadeTypes.h"
#include "ShadeGameMode.generated.h"

class AShadeCharacter;
class AShadeNapCat;
class ACameraActor;
class UAudioComponent;
class ADirectionalLight;

USTRUCT()
struct FShadeObjective
{
	GENERATED_BODY()

	FName CompleteOn;
	/** Actor tag of the world point the objective marker floats over. */
	FName Target;
	FText Title;
	FText Hint;
};

USTRUCT()
struct FShadeToast
{
	GENERATED_BODY()

	FText Text;
	double StartTime = 0.0;
};

/**
 * Owns the run: Title -> Intro -> Playing -> Finale (the bell rings, the sun sinks and the bell-tower shadow sweeps
 * across the terrace) -> Reunion cinematic -> Ended.
 * Gameplay actors report beats through NotifyEvent(); objectives, narration, music and the sun react to them.
 * Command line: -autoplay (self-playing demo), -autoplayexit, -autoplayshots, -sunmap, -catgallery, -skiptitle.
 */
UCLASS()
class SHADENSEEK_API AShadeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AShadeGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	static bool IsAutoplay();
	static bool IsAutoplayExit();
	static bool HasParam(const TCHAR* Param);
	/**
	 * Render budget for 1080p at 90-120 fps on a 6 GB laptop RTX 4050: High scalability with a few hand-picked
	 * cvars and dynamic resolution (TSR upscales). Applied by code at startup so a saved Epic config cannot undo it;
	 * -epic skips it (old look, for comparisons) and -ExecCmds still override it.
	 */
	static void ApplyRenderBudget();

	// --- Flow -----------------------------------------------------------------------------------------------------
	EShadePhase GetPhase() const { return Phase; }
	double GetPhaseStartRealTime() const { return PhaseStartRealTime; }
	float GetPhaseTime() const { return PhaseTime; }
	void StartGame();
	void RestartRun();
	/** Health ran out: fade and reload straight into the lane. */
	void ShadeFaded();
	bool IsRetry() const { return bRetry; }
	void QuitGame();

	// --- Story ----------------------------------------------------------------------------------------------------
	void NotifyEvent(FName EventName, AActor* Source);
	bool HasEvent(FName EventName) const { return Events.Contains(EventName); }
	bool CanReunite() const;
	void BeginReunion(AShadeNapCat* Cat);

	const FShadeObjective* GetObjective() const { return Objectives.IsValidIndex(ObjectiveIndex) ? &Objectives[ObjectiveIndex] : nullptr; }
	int32 GetObjectiveIndex() const { return ObjectiveIndex; }
	int32 GetObjectiveCount() const { return Objectives.Num(); }
	double GetObjectiveChangeTime() const { return ObjectiveChangeTime; }
	bool GetObjectiveTargetLocation(FVector& Out) const;

	void Say(const FText& Line, float Hold = 4.5f);
	const FText& GetLine() const { return Line; }
	double GetLineStartTime() const { return LineStartTime; }
	float GetLineHold() const { return LineHold; }

	void PushToast(const FText& Text);
	const TArray<FShadeToast>& GetToasts() const { return Toasts; }

	// --- Presentation ---------------------------------------------------------------------------------------------
	void FadeTo(float Target, float Duration, const FLinearColor& Color, float Delay = 0.f);
	float GetFadeAlpha() const { return FadeAlpha; }
	FLinearColor GetFadeColor() const { return FadeColor; }
	float GetLetterbox() const { return Letterbox; }
	float GetRunTime() const { return RunTime; }
	float GetFinaleAlpha() const { return FinaleAlpha; }
	AShadeCharacter* GetShade() const;
	AShadeNapCat* GetBiscotto() const { return Biscotto.Get(); }
	void AddTrauma(float Amount);
	void SpawnDust(const FVector& Location, float Strength);
	int32 GetPuzzlesSolved() const { return PuzzlesSolved; }

protected:
	void SetPhase(EShadePhase NewPhase);
	void BuildObjectives();
	void AdvanceObjectives();
	void TickTitle(float Dt);
	void TickFinale(float Dt);
	void TickReunion(float Dt);
	void RunCatGallery();
	void RunCamShots();
	void WriteSunMap(const TCHAR* Suffix);
	void TickMotionCap(float Dt);

	/**
	 * QA motion capture (-motioncap=title|follow|side|still|jumps, run with -benchmark -fps=30 for a fixed time step):
	 * drives a scripted walk / turn / stop / about-turn / trot / jump (still: no input, for render flicker; jumps: hops
	 * in place, walking, trotting and with the camera swung at a wall), screenshots every frame to
	 * Saved/MotionCap/<mode>/ and writes bone + camera tracks (plus body visibility and camera line of sight) to a CSV,
	 * so motion problems (jitter, tail spin, gait, a vanishing cat) can be measured, not guessed.
	 */
	FString MotionCap;
	float CapYaw = 0.f;
	float CapTime = 0.f;
	int32 CapFrame = 0;
	FString CapCsv;
	FVector CapStart = FVector::ZeroVector;
	FVector CapFwd = FVector::ForwardVector;
	FVector CapCamFocus = FVector::ZeroVector;
	bool bCapInit = false;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CapCam;

	/**
	 * -perflog: wall-clock frame times while Shade is in play (works in Shipping, where the CSV profiler does not);
	 * the summary goes to Saved/Perf/perflog.txt when the game quits.
	 */
	TArray<float> PerfFrames;
	double PerfLast = 0.0;
	void WritePerfLog();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	EShadePhase Phase = EShadePhase::Title;
	double PhaseStartRealTime = 0.0;
	float PhaseTime = 0.f;

	TArray<FShadeObjective> Objectives;
	int32 ObjectiveIndex = 0;
	double ObjectiveChangeTime = 0.0;
	TSet<FName> Events;
	mutable TWeakObjectPtr<AActor> CachedTarget;
	mutable int32 CachedTargetIndex = -1;
	TMap<FName, FText> EventLines;

	FText Line;
	double LineStartTime = -1000.0;
	float LineHold = 0.f;
	TArray<FShadeToast> Toasts;

	float FadeAlpha = 1.f;
	float FadeTarget = 0.f;
	float FadeSpeed = 1.f;
	float FadeDelay = 0.f;
	FLinearColor FadeColor = FLinearColor::Black;
	float Letterbox = 0.f;
	float LetterboxTarget = 0.f;
	float RunTime = 0.f;
	int32 PuzzlesSolved = 0;
	bool bSkipTitle = false;
	bool bRetry = false;
	bool bFading = false;
	bool bSunMapPending = false;
	float SunMapTimer = 0.f;
	float ShotTimer = 0.f;
	int32 ShotIndex = 0;
	float ExitTimer = -1.f;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TitleCamera;
	FTransform TitleCameraBase;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> CineCamera;

	TWeakObjectPtr<AShadeNapCat> Biscotto;
	TWeakObjectPtr<ADirectionalLight> Sun;
	FRotator SunStart = FRotator::ZeroRotator;
	FRotator SunEnd = FRotator::ZeroRotator;
	bool bHasSunEnd = false;
	float FinaleAlpha = 0.f;
	float FinaleTime = 0.f;
	int32 BellsRung = 0;
	float ReunionTime = 0.f;
	int32 ReunionStep = 0;
	FVector ReunionSpot = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Music;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbSea;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbVillage;

	float MusicTarget = 0.5f;
	float MusicLevel = 0.f;
};
