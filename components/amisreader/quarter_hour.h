#pragma once

// Viertelstunden-Leistung und Monatsmaximum, berechnet aus dem Zaehlerstand Wirkenergie Bezug (1-0:1.8.0).
//
// Die Berechnung ist unabhaengig von ESPHome. Zeitgrundlage ist ausschliesslich die Zaehlerzeit aus dem
// Telegramm, damit die Messperioden zu denen des Zaehlers passen (Abrechnung nach Zaehlerzeit).
//
// Verfahren:
//  - Die Grenzen der Viertelstunden liegen auf hh:00, hh:15, hh:30, hh:45 der Zaehlerzeit.
//  - Der Zaehlerstand an einer Grenze wird zwischen dem letzten Telegramm davor und dem ersten Telegramm
//    danach linear interpoliert. Dadurch genuegt es, wenn einzelne Telegramme verloren gehen.
//  - Leistung der Viertelstunde = Energie der Viertelstunde * 4 (Wh -> W).
//  - Das Monatsmaximum gehoert zu dem Monat, in dem die Viertelstunde begann, und wird mit der Zaehlerzeit am Ende
//    der Viertelstunde, in der es entstand, gemerkt.
//  - Bei Luecken (> MAX_GAP_S), Zeitspruengen oder rueckwaerts laufendem Zaehlerstand wird nicht ueber die Luecke
//    hinweg gerechnet. Die betroffene Viertelstunde wird verworfen.

#include <stdint.h>

namespace esphome {
namespace amis {

// Dieser Teil wird im Flash gesichert und ueberlebt einen Neustart.
struct QuarterPersist {
  int32_t month_id;       // Jahr * 12 + (Monat - 1) der Zaehlerzeit, 0 = noch kein Wert
  double max_w;           // Monatsmaximum in W
  int64_t max_end_epoch;  // Zaehlerzeit (Epoche) am Ende der Viertelstunde des Maximums, 0 = unbekannt
};

class QuarterHour {
 public:
  static constexpr int64_t PERIOD_S = 900;      // Messperiode: 15 Minuten
  static constexpr int64_t MAX_GAP_S = 60;      // groesster Abstand zweier Telegramme, ueber den interpoliert wird
  static constexpr int64_t MIN_ELAPSED_S = 60;  // laufende Viertelstunde wird erst nach dieser Zeit angegeben

  enum : uint8_t { CHANGED_QUARTER = 1, CHANGED_MAX = 2 };

  void restore(const QuarterPersist &p) { this->persist_ = p; }
  const QuarterPersist &persist() const { return this->persist_; }

  // Zaehlerzeit oder Zaehlerstand fehlen/sind ungueltig: die Kette der Telegramme ist unterbrochen.
  void invalidate() {
    this->have_prev_ = false;
    this->have_start_ = false;
    this->current_valid_ = false;
  }

  // local_s: Zaehlerzeit in Sekunden (ohne Zeitzone), epoch_s: dieselbe Zeit als Unix-Zeit (nur fuer die Zeitangabe),
  // month_id: Monat der Zaehlerzeit, energy_wh: Zaehlerstand Wirkenergie Bezug in Wh.
  // Rueckgabe: Bitmaske CHANGED_QUARTER (Viertelstunde abgeschlossen) und/oder CHANGED_MAX (Maximum neu/zurueckgesetzt).
  uint8_t update(int64_t local_s, int64_t epoch_s, int32_t month_id, double energy_wh) {
    uint8_t changed = 0;
    bool boundary_crossed = false;
    const bool continuous = this->have_prev_ && local_s > this->prev_local_ && (local_s - this->prev_local_) <= MAX_GAP_S &&
                            energy_wh >= this->prev_e_;

    if (!continuous) {
      // Erstes Telegramm, Luecke oder Zeitsprung: laufende Viertelstunde ist unbekannt.
      this->have_start_ = false;
      this->current_valid_ = false;
      if (local_s % PERIOD_S == 0) {
        this->start_e_ = energy_wh;
        this->start_local_ = local_s;
        this->have_start_ = true;
      }
    } else {
      // Alle Viertelstunden-Grenzen im Intervall (prev_local_, local_s]
      int64_t b = (this->prev_local_ / PERIOD_S + 1) * PERIOD_S;
      while (b <= local_s) {
        boundary_crossed = true;
        const double eb = this->prev_e_ + (energy_wh - this->prev_e_) * (double) (b - this->prev_local_) /
                                              (double) (local_s - this->prev_local_);
        if (this->have_start_) {
          const double p = (eb - this->start_e_) * (3600.0 / (double) PERIOD_S);
          this->last_w_ = p;
          this->last_end_epoch_ = epoch_s - (local_s - b);
          this->last_valid_ = true;
          changed |= CHANGED_QUARTER;

          // Die Viertelstunde gehoert zum Monat, in dem sie begann (= Monat des letzten Telegramms davor).
          if (this->persist_.month_id != this->prev_month_) {
            this->new_month_(this->prev_month_);
            changed |= CHANGED_MAX;
          }
          if (p > this->persist_.max_w) {
            this->persist_.max_w = p;
            this->persist_.max_end_epoch = this->last_end_epoch_;
            changed |= CHANGED_MAX;
          }
        }
        this->start_e_ = eb;
        this->start_local_ = b;
        this->have_start_ = true;
        b += PERIOD_S;
      }
    }

    // Monatswechsel: Maximum zuruecksetzen. Wurde in diesem Telegramm gerade die letzte Viertelstunde des alten Monats
    // abgeschlossen, bleibt deren Ergebnis noch einen Moment sichtbar und das Zuruecksetzen folgt mit dem naechsten Telegramm.
    if (!boundary_crossed && this->persist_.month_id != month_id) {
      this->new_month_(month_id);
      changed |= CHANGED_MAX;
    }

    // Leistung der laufenden Viertelstunde = Mittelwert seit Beginn der Viertelstunde
    this->current_valid_ = false;
    if (this->have_start_) {
      const int64_t elapsed = local_s - this->start_local_;
      if (elapsed >= MIN_ELAPSED_S && energy_wh >= this->start_e_) {
        this->current_w_ = (energy_wh - this->start_e_) * 3600.0 / (double) elapsed;
        this->current_valid_ = true;
      }
    }

    this->prev_local_ = local_s;
    this->prev_e_ = energy_wh;
    this->prev_month_ = month_id;
    this->have_prev_ = true;
    return changed;
  }

  bool current_valid() const { return this->current_valid_; }
  double current_w() const { return this->current_w_; }
  bool last_valid() const { return this->last_valid_; }
  double last_w() const { return this->last_w_; }
  int64_t last_end_epoch() const { return this->last_end_epoch_; }
  bool max_known() const { return this->persist_.month_id != 0; }
  double max_w() const { return this->persist_.max_w; }
  int64_t max_end_epoch() const { return this->persist_.max_end_epoch; }

 protected:
  void new_month_(int32_t month_id) {
    this->persist_.month_id = month_id;
    this->persist_.max_w = 0;
    this->persist_.max_end_epoch = 0;
  }

  QuarterPersist persist_{};

  bool have_prev_{false};
  int64_t prev_local_{0};
  double prev_e_{0};
  int32_t prev_month_{0};

  bool have_start_{false};
  int64_t start_local_{0};
  double start_e_{0};

  bool current_valid_{false};
  double current_w_{0};
  bool last_valid_{false};
  double last_w_{0};
  int64_t last_end_epoch_{0};
};

}  // namespace amis
}  // namespace esphome
