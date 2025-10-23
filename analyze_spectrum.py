#!/usr/bin/env python3
"""
Analyze spectrum of IQ file to detect frequency offset
"""

import numpy as np
import matplotlib.pyplot as plt
import sys

def load_iq_file(filename):
    """Load complex float32 IQ file"""
    print(f"Loading {filename}...")

    # Read as interleaved I/Q float32
    data = np.fromfile(filename, dtype=np.float32)

    # Convert to complex
    iq = data[::2] + 1j * data[1::2]

    print(f"  Samples: {len(iq)}")
    print(f"  Duration: {len(iq) / 2.5e6:.3f} seconds @ 2.5 MHz")

    return iq

def analyze_spectrum(iq, sample_rate=2.5e6, fft_size=8192):
    """Analyze spectrum and find peak"""
    print(f"\nComputing FFT (size={fft_size})...")

    # Compute power spectrum (average over multiple FFTs)
    num_ffts = len(iq) // fft_size
    psd = np.zeros(fft_size)

    for i in range(num_ffts):
        segment = iq[i*fft_size:(i+1)*fft_size]

        # Apply Hanning window
        window = np.hanning(fft_size)
        segment_windowed = segment * window

        # FFT
        fft = np.fft.fftshift(np.fft.fft(segment_windowed))
        psd += np.abs(fft)**2

    psd /= num_ffts
    psd_db = 10 * np.log10(psd + 1e-12)

    # Frequency axis
    freqs = np.fft.fftshift(np.fft.fftfreq(fft_size, 1/sample_rate))

    # Find peak (exclude DC bin ±1 kHz)
    dc_mask = (np.abs(freqs) > 1000)
    peak_idx = np.argmax(psd_db[dc_mask])
    peak_freq = freqs[dc_mask][peak_idx]
    peak_power = psd_db[dc_mask][peak_idx]

    print(f"\nSpectrum Analysis:")
    print(f"  Peak frequency: {peak_freq/1e3:.2f} kHz")
    print(f"  Peak power: {peak_power:.1f} dB")
    print(f"  FFTs averaged: {num_ffts}")

    # Find 3dB bandwidth
    threshold = peak_power - 3
    above_threshold = psd_db > threshold
    if np.any(above_threshold):
        bandwidth_bins = np.sum(above_threshold)
        bandwidth = bandwidth_bins * (sample_rate / fft_size)
        print(f"  3dB bandwidth: {bandwidth/1e3:.1f} kHz")

    return freqs, psd_db, peak_freq

def plot_spectrum(freqs, psd_db, peak_freq, output_file='spectrum_analysis.png'):
    """Plot spectrum with annotations"""
    print(f"\nGenerating plot: {output_file}")

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(12, 8))

    # Full spectrum
    ax1.plot(freqs/1e3, psd_db, linewidth=0.8)
    ax1.axvline(peak_freq/1e3, color='r', linestyle='--',
                label=f'Peak: {peak_freq/1e3:.2f} kHz')
    ax1.axvline(0, color='gray', linestyle=':', alpha=0.5, label='DC (0 Hz)')
    ax1.grid(True, alpha=0.3)
    ax1.set_xlabel('Frequency (kHz)')
    ax1.set_ylabel('Power (dB)')
    ax1.set_title('Full Spectrum: test_sgb.iq @ 2.5 MHz')
    ax1.legend()
    ax1.set_xlim([-1250, 1250])  # ±1.25 MHz

    # Zoomed around peak
    zoom_range = 50  # ±50 kHz
    zoom_mask = np.abs(freqs - peak_freq) < zoom_range * 1e3

    ax2.plot(freqs[zoom_mask]/1e3, psd_db[zoom_mask], linewidth=1.5)
    ax2.axvline(peak_freq/1e3, color='r', linestyle='--',
                label=f'Peak: {peak_freq/1e3:.2f} kHz')
    ax2.axvline(0, color='gray', linestyle=':', alpha=0.5, label='Expected (0 Hz)')
    ax2.grid(True, alpha=0.3)
    ax2.set_xlabel('Frequency (kHz)')
    ax2.set_ylabel('Power (dB)')
    ax2.set_title(f'Zoomed Spectrum (±{zoom_range} kHz around peak)')
    ax2.legend()

    plt.tight_layout()
    plt.savefig(output_file, dpi=150)
    print(f"  Saved: {output_file}")

    return fig

def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_spectrum.py <file.iq>")
        sys.exit(1)

    filename = sys.argv[1]

    # Load IQ
    iq = load_iq_file(filename)

    # Analyze spectrum
    freqs, psd_db, peak_freq = analyze_spectrum(iq, sample_rate=2.5e6, fft_size=8192)

    # Plot
    output_file = filename.replace('.iq', '_spectrum.png')
    plot_spectrum(freqs, psd_db, peak_freq, output_file)

    # Summary
    print("\n" + "="*60)
    print("SUMMARY:")
    print("="*60)
    print(f"File: {filename}")
    print(f"Peak frequency offset: {peak_freq/1e3:.2f} kHz")
    print(f"Expected: 0.0 kHz (baseband centered)")

    if abs(peak_freq) > 1000:
        print(f"\n⚠️  WARNING: Signal offset by {peak_freq/1e3:.2f} kHz!")
        print("   Possible causes:")
        print("   - Generator bug (not centering signal)")
        print("   - SDR oscillator offset")
        print("   - Intentional frequency shift")
    else:
        print("\n✅ Signal properly centered at baseband")

    print("="*60)

if __name__ == '__main__':
    main()
