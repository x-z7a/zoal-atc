// Command mockconsole is a stand-in for the console's traffic component, for
// developing the plugin before atc.zoal.app serves `traffic` frames.
//
// It speaks just enough of /plugin: it waits for the aircraft's first frame
// (the client discards anything sharing a segment with the handshake), reads
// hello and telemetry, and once a second sends each connected aircraft a
// traffic frame with
//
//   - every other connected aircraft, as a participant (two simulators on one
//     mock see each other: the Plane-Pals case), and
//   - a few AI aircraft flying a racetrack around where that aircraft first
//     reported, standing in for console-controlled traffic.
//
// Usage: go run ./tools/mockconsole -listen :8766 -ai 4
// and in <plugin>/zoal-mp.cfg:  url = ws://127.0.0.1:8766/plugin
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math"
	"net/http"
	"sync"
	"time"

	"github.com/coder/websocket"
)

type telemetry struct {
	Lat      float64 `json:"latitude_deg"`
	Lon      float64 `json:"longitude_deg"`
	Alt      float64 `json:"altitude_ft_msl"`
	GS       float64 `json:"groundspeed_kts"`
	VS       float64 `json:"vertical_speed_fpm"`
	Heading  float64 `json:"heading_true_deg"`
	Track    float64 `json:"track_deg"`
	Pitch    float64 `json:"pitch_deg"`
	Roll     float64 `json:"roll_deg"`
	OnGround bool    `json:"on_ground"`
	Gear     float64 `json:"gear"`
	Flaps    float64 `json:"flaps"`
	Brake    float64 `json:"speedbrake"`
	Thrust   float64 `json:"thrust"`
	Lights   uint32  `json:"lights"`
	Callsign string  `json:"callsign"`
	ICAOType string  `json:"icao_type"`
	Livery   string  `json:"livery"`
}

type inbound struct {
	Type      string     `json:"type"`
	ClientID  string     `json:"client_id"`
	Telemetry *telemetry `json:"telemetry"`
}

// target is one element of a traffic frame, in docs/protocol.md order.
type target struct {
	ID       string  `json:"id"`
	Kind     string  `json:"kind"`
	TMs      int64   `json:"t_ms"`
	ModeS    uint32  `json:"mode_s"`
	Lat      float64 `json:"lat_deg"`
	Lon      float64 `json:"lon_deg"`
	Alt      float64 `json:"alt_ft_msl"`
	Heading  float64 `json:"heading_deg"`
	Pitch    float64 `json:"pitch_deg"`
	Roll     float64 `json:"roll_deg"`
	GS       float64 `json:"groundspeed_kts"`
	VS       float64 `json:"vertical_speed_fpm"`
	Track    float64 `json:"track_deg"`
	OnGround bool    `json:"on_ground"`
	Gear     float64 `json:"gear"`
	Flaps    float64 `json:"flaps"`
	Brake    float64 `json:"speedbrake"`
	Thrust   float64 `json:"thrust"`
	Lights   uint32  `json:"lights"`
	ICAOType string  `json:"icao_type"`
	Airline  string  `json:"airline"`
	Livery   string  `json:"livery"`
	Callsign string  `json:"callsign"`
}

type frame struct {
	Type       string   `json:"type"`
	Seq        uint64   `json:"seq"`
	ServerTime int64    `json:"server_time_ms"`
	Targets    []target `json:"targets"`
	Removed    []string `json:"removed"`
}

type pilot struct {
	id     string
	last   telemetry
	seenAt time.Time
	origin *[2]float64 // where the AI racetrack is centred
}

type hub struct {
	mu     sync.Mutex
	pilots map[string]*pilot
	ai     int
	start  time.Time
}

func (h *hub) update(id string, t telemetry) *pilot {
	h.mu.Lock()
	defer h.mu.Unlock()
	p := h.pilots[id]
	if p == nil {
		p = &pilot{id: id}
		h.pilots[id] = p
	}
	p.last, p.seenAt = t, time.Now()
	if p.origin == nil && (t.Lat != 0 || t.Lon != 0) {
		p.origin = &[2]float64{t.Lat, t.Lon}
	}
	return p
}

func (h *hub) drop(id string) {
	h.mu.Lock()
	defer h.mu.Unlock()
	delete(h.pilots, id)
}

var airlines = []struct{ op, typ string }{{"UAL", "B738"}, {"SWA", "B737"}, {"DAL", "A321"}, {"ASA", "B739"}, {"SKW", "E75L"}, {"AAL", "A320"}}

// racetrack places AI aircraft i on a 12 nm x 4 nm oval around the origin at
// 3000 ft + 1000 ft per aircraft, 180 kt, spaced evenly in time.
func racetrack(i, n int, origin [2]float64, now time.Time, start time.Time) target {
	const speedKts = 180.0
	const straightNM, radiusNM = 12.0, 2.0
	perimeter := 2*straightNM + 2*math.Pi*radiusNM
	period := perimeter / speedKts * 3600
	phase := math.Mod(now.Sub(start).Seconds()/period+float64(i)/float64(n), 1) * perimeter

	var x, y, track float64 // nm east/north of origin, degrees
	switch {
	case phase < straightNM: // northbound east leg
		x, y, track = radiusNM, -straightNM/2+phase, 0
	case phase < straightNM+math.Pi*radiusNM: // north turn, east to west
		a := (phase - straightNM) / radiusNM
		x, y, track = radiusNM*math.Cos(a), straightNM/2+radiusNM*math.Sin(a), math.Mod(360-a*180/math.Pi, 360)
	case phase < 2*straightNM+math.Pi*radiusNM: // southbound west leg
		x, y, track = -radiusNM, straightNM/2-(phase-straightNM-math.Pi*radiusNM), 180
	default: // south turn, west to east
		a := (phase - 2*straightNM - math.Pi*radiusNM) / radiusNM
		x, y, track = -radiusNM*math.Cos(a), -straightNM/2-radiusNM*math.Sin(a), 180-a*180/math.Pi
	}
	roll := 0.0
	if track != 0 && track != 180 {
		roll = -25
	}
	lat := origin[0] + y/60
	lon := origin[1] + x/(60*math.Cos(origin[0]*math.Pi/180))
	al := airlines[i%len(airlines)]
	cs := fmt.Sprintf("%s%d", al.op, 100+i)
	return target{
		ID: "ai:" + cs, Kind: "ai", TMs: now.UnixMilli(), ModeS: 0xC0FFE0 + uint32(i),
		Lat: lat, Lon: lon, Alt: 3000 + 1000*float64(i), Heading: track, Track: track, Roll: roll,
		GS: speedKts, Gear: 0, Flaps: 0, Thrust: 0.6, Lights: 1 | 2 | 4,
		ICAOType: al.typ, Airline: al.op, Callsign: cs,
	}
}

func (h *hub) frameFor(id string, seq uint64) frame {
	h.mu.Lock()
	defer h.mu.Unlock()
	now := time.Now()
	f := frame{Type: "traffic", Seq: seq, ServerTime: now.UnixMilli(), Targets: []target{}, Removed: []string{}}
	for otherID, p := range h.pilots {
		if otherID == id || now.Sub(p.seenAt) > 10*time.Second {
			continue
		}
		t := p.last
		f.Targets = append(f.Targets, target{
			ID: "participant:" + otherID, Kind: "participant", TMs: p.seenAt.UnixMilli(),
			Lat: t.Lat, Lon: t.Lon, Alt: t.Alt, Heading: t.Heading, Pitch: t.Pitch, Roll: t.Roll,
			GS: t.GS, VS: t.VS, Track: t.Track, OnGround: t.OnGround, Gear: t.Gear, Flaps: t.Flaps,
			Brake: t.Brake, Thrust: t.Thrust, Lights: t.Lights,
			ICAOType: t.ICAOType, Livery: t.Livery, Callsign: t.Callsign,
		})
	}
	if me := h.pilots[id]; me != nil && me.origin != nil {
		for i := 0; i < h.ai; i++ {
			f.Targets = append(f.Targets, racetrack(i, h.ai, *me.origin, now, h.start))
		}
	}
	return f
}

func encode(v any) ([]byte, error) {
	// Same rule as the console's wire.Encode: no HTML escaping.
	var buf jsonBuffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	err := enc.Encode(v)
	return buf.b, err
}

type jsonBuffer struct{ b []byte }

func (j *jsonBuffer) Write(p []byte) (int, error) { j.b = append(j.b, p...); return len(p), nil }

func (h *hub) serve(w http.ResponseWriter, r *http.Request) {
	conn, err := websocket.Accept(w, r, nil)
	if err != nil {
		return
	}
	defer conn.CloseNow()
	conn.SetReadLimit(1 << 20)
	ctx, cancel := context.WithCancel(r.Context())
	defer cancel()

	id := r.RemoteAddr
	first := make(chan struct{})
	go func() {
		defer cancel()
		once := sync.Once{}
		for {
			_, data, err := conn.Read(ctx)
			if err != nil {
				return
			}
			once.Do(func() { close(first) })
			var msg inbound
			if json.Unmarshal(data, &msg) != nil {
				continue
			}
			switch msg.Type {
			case "hello":
				if msg.ClientID != "" {
					id = msg.ClientID
				}
				log.Printf("hello from %s", id)
			case "telemetry":
				if msg.Telemetry != nil {
					h.update(id, *msg.Telemetry)
				}
			}
		}
	}()

	select {
	case <-first:
	case <-ctx.Done():
		return
	}
	defer h.drop(id)

	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()
	var seq uint64
	for {
		select {
		case <-ctx.Done():
			log.Printf("%s gone", id)
			return
		case <-ticker.C:
			seq++
			data, err := encode(h.frameFor(id, seq))
			if err != nil {
				return
			}
			if err := conn.Write(ctx, websocket.MessageText, data); err != nil {
				return
			}
		}
	}
}

func main() {
	listen := flag.String("listen", ":8766", "address to listen on")
	ai := flag.Int("ai", 4, "AI aircraft per pilot")
	flag.Parse()
	h := &hub{pilots: map[string]*pilot{}, ai: *ai, start: time.Now()}
	http.HandleFunc("/plugin", h.serve)
	log.Printf("mock console on %s/plugin, %d AI aircraft per pilot", *listen, *ai)
	log.Fatal(http.ListenAndServe(*listen, nil))
}
