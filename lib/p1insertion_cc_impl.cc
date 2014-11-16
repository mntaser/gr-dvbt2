/* -*- c++ -*- */
/* 
 * Copyright 2014 Ron Economos.
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "p1insertion_cc_impl.h"
#include <math.h>
#include <volk/volk.h>
#include <stdio.h>

namespace gr {
  namespace dvbt2 {

    p1insertion_cc::sptr
    p1insertion_cc::make(dvbt2_extended_carrier_t carriermode, dvbt2_fftsize_t fftsize, dvbt2_guardinterval_t guardinterval, int numdatasyms, const std::vector<float> &window)
    {
      return gnuradio::get_initial_sptr
        (new p1insertion_cc_impl(carriermode, fftsize, guardinterval, numdatasyms, window));
    }

    /*
     * The private constructor
     */
    p1insertion_cc_impl::p1insertion_cc_impl(dvbt2_extended_carrier_t carriermode, dvbt2_fftsize_t fftsize, dvbt2_guardinterval_t guardinterval, int numdatasyms, const std::vector<float> &window)
      : gr::block("p1insertion_cc",
              gr::io_signature::make(1, 1, sizeof(gr_complex)),
              gr::io_signature::make(1, 1, sizeof(gr_complex)))
    {
        int s1, s2, index = 0;
        const gr_complex *in = (const gr_complex *) p1_freq;
        gr_complex *out = (gr_complex *) p1_time;
        switch (fftsize)
        {
            case gr::dvbt2::FFTSIZE_1K:
                fft_size = 1024;
                N_P2 = 16;
                C_PS = 853;
                break;
            case gr::dvbt2::FFTSIZE_2K:
                fft_size = 2048;
                N_P2 = 8;
                C_PS = 1705;
                break;
            case gr::dvbt2::FFTSIZE_4K:
                fft_size = 4096;
                N_P2 = 4;
                C_PS = 3409;
                break;
            case gr::dvbt2::FFTSIZE_8K_NORM:
            case gr::dvbt2::FFTSIZE_8K_SGI:
                fft_size = 8192;
                N_P2 = 2;
                if (carriermode == gr::dvbt2::CARRIERS_NORMAL)
                {
                    C_PS = 6817;
                }
                else
                {
                    C_PS = 6913;
                }
                break;
            case gr::dvbt2::FFTSIZE_16K:
                fft_size = 16384;
                N_P2 = 1;
                if (carriermode == gr::dvbt2::CARRIERS_NORMAL)
                {
                    C_PS = 13633;
                }
                else
                {
                    C_PS = 13921;
                }
                break;
            case gr::dvbt2::FFTSIZE_32K_NORM:
            case gr::dvbt2::FFTSIZE_32K_SGI:
                fft_size = 32768;
                N_P2 = 1;
                if (carriermode == gr::dvbt2::CARRIERS_NORMAL)
                {
                    C_PS = 27265;
                }
                else
                {
                    C_PS = 27841;
                }
                break;
            default:
                fft_size = 0;
                N_P2 = 0;
                C_PS = 0;
                break;
        }
        normalization = 5.0 / sqrt(27.0 * C_PS);
        switch (guardinterval)
        {
            case gr::dvbt2::GI_1_32:
                guard_interval = fft_size / 32;
                break;
            case gr::dvbt2::GI_1_16:
                guard_interval = fft_size / 16;
                break;
            case gr::dvbt2::GI_1_8:
                guard_interval = fft_size / 8;
                break;
            case gr::dvbt2::GI_1_4:
                guard_interval = fft_size / 4;
                break;
            case gr::dvbt2::GI_1_128:
                guard_interval = fft_size / 128;
                break;
            case gr::dvbt2::GI_19_128:
                guard_interval = (fft_size * 19) / 128;
                break;
            case gr::dvbt2::GI_19_256:
                guard_interval = (fft_size * 19) / 256;
                break;
            default:
                guard_interval = 0;
                break;
        }
        init_p1_randomizer();
        s1 = PREAMBLE_T2_SISO;
        s2 = fftsize << 1;
        for (int i = 0; i < 8; i++)
        {
            for (int j = 7; j >= 0; j--)
            {
                modulation_sequence[index++] = (s1_modulation_patterns[s1][i] >> j) & 0x1;
            }
        }
        for (int i = 0; i < 32; i++)
        {
            for (int j = 7; j >= 0; j--)
            {
                modulation_sequence[index++] = (s2_modulation_patterns[s2][i] >> j) & 0x1;
            }
        }
        for (int i = 0; i < 8; i++)
        {
            for (int j = 7; j >= 0; j--)
            {
                modulation_sequence[index++] = (s1_modulation_patterns[s1][i] >> j) & 0x1;
            }
        }
        dbpsk_modulation_sequence[0] = 1;
        for (int i = 1; i < 385; i++)
        {
            dbpsk_modulation_sequence[i] = 0;
        }
        for (int i = 1; i < 385; i++)
        {
            if (modulation_sequence[i - 1] == 1)
            {
                dbpsk_modulation_sequence[i] = -dbpsk_modulation_sequence[i - 1];
            }
            else
            {
                dbpsk_modulation_sequence[i] = dbpsk_modulation_sequence[i - 1];
            }
        }
        for (int i = 0; i < 384; i++)
        {
            dbpsk_modulation_sequence[i] = dbpsk_modulation_sequence[i + 1] * p1_randomize[i];
        }
        for (int i = 0; i < 1024; i++)
        {
            p1_freq[i].real() = 0.0;
            p1_freq[i].imag() = 0.0;
        }
        for (int i = 0; i < 384; i++)
        {
            p1_freq[p1_active_carriers[i] + 86].real() = float(dbpsk_modulation_sequence[i]);
        }
        p1_fft_size = 1024;
        p1_fft = new fft::fft_complex(p1_fft_size, false, 1);
        p1_window = window;
        gr_complex *dst = p1_fft->get_inbuf();
        unsigned int offset = p1_fft_size / 2;
        int fft_m_offset = p1_fft_size - offset;
        for(unsigned int i = 0; i < offset; i++)
        {
            dst[i+fft_m_offset] = in[i] * p1_window[i];
        }
        for(unsigned int i = offset; i < p1_fft_size; i++)
        {
            dst[i-offset] = in[i] * p1_window[i];
        }
        p1_fft->execute();
        memcpy(out, p1_fft->get_outbuf(), sizeof(gr_complex) * p1_fft_size);
        for (int i = 0; i < 1024; i++)
        {
            p1_time[i].real() *= 1 / sqrt(384);
            p1_time[i].imag() *= 1 / sqrt(384);
        }
        for (int i = 0; i < 1023; i++)
        {
            p1_freqshft[i + 1] = p1_freq[i];
        }
        p1_freqshft[0] = p1_freq[1023];
        in = (const gr_complex *) p1_freqshft;
        out = (gr_complex *) p1_timeshft;
        dst = p1_fft->get_inbuf();
        for(unsigned int i = 0; i < offset; i++)
        {
            dst[i+fft_m_offset] = in[i] * p1_window[i];
        }
        for(unsigned int i = offset; i < p1_fft_size; i++)
        {
            dst[i-offset] = in[i] * p1_window[i];
        }
        p1_fft->execute();
        memcpy(out, p1_fft->get_outbuf(), sizeof(gr_complex) * p1_fft_size);
        for (int i = 0; i < 1024; i++)
        {
            p1_timeshft[i].real() *= 1 / sqrt(384);
            p1_timeshft[i].imag() *= 1 / sqrt(384);
        }
        const int alignment_multiple = volk_get_alignment() / sizeof(gr_complex);
        set_alignment(std::max(1, alignment_multiple));
        frame_items = ((numdatasyms + N_P2) * fft_size) + ((numdatasyms + N_P2) * guard_interval);
        insertion_items = frame_items + 2048;
        set_output_multiple(frame_items + 2048);
    }

void p1insertion_cc_impl::init_p1_randomizer(void)
{
    int sr = 0x4e46;
    for (int i = 0; i < 384; i++)
    {
        int b = ((sr) ^ (sr >> 1)) & 1;
        if (b == 0)
        {
           p1_randomize[i] = 1;
        }
        else
        {
           p1_randomize[i] = -1;
        }
        sr >>= 1;
        if(b) sr |= 0x4000;
    }
}

    /*
     * Our virtual destructor.
     */
    p1insertion_cc_impl::~p1insertion_cc_impl()
    {
        delete p1_fft;
    }

    void
    p1insertion_cc_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
        ninput_items_required[0] = frame_items * (noutput_items / insertion_items);
    }

    int
    p1insertion_cc_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
        const gr_complex *in = (const gr_complex *) input_items[0];
        gr_complex *out = (gr_complex *) output_items[0];

        for (int i = 0; i < noutput_items; i += insertion_items)
        {
            for (int j = 0; j < 542; j++)
            {
                *out++ = p1_timeshft[j];
            }
            for (int j = 0; j < 1024; j++)
            {
                *out++ = p1_time[j];
            }
            for (int j = 542; j < 1024; j++)
            {
                *out++ = p1_timeshft[j];
            }
            volk_32fc_s32fc_multiply_32fc(out, in, normalization, frame_items);
            out += frame_items;
            in += frame_items;
        }

        // Tell runtime system how many input items we consumed on
        // each input stream.
        consume_each (frame_items);

        // Tell runtime system how many output items we produced.
        return noutput_items;
    }

    const int p1insertion_cc_impl::p1_active_carriers[384] = 
    {
        44, 45, 47, 51, 54, 59, 62, 64, 65, 66, 70, 75, 78, 80, 81, 82, 84, 85, 87, 88, 89, 90,
        94, 96, 97, 98, 102, 107, 110, 112, 113, 114, 116, 117, 119, 120, 121, 122, 124,
        125, 127, 131, 132, 133, 135, 136, 137, 138, 142, 144, 145, 146, 148, 149, 151,
        152, 153, 154, 158, 160, 161, 162, 166, 171,

        172, 173, 175, 179, 182, 187, 190, 192, 193, 194, 198, 203, 206, 208, 209, 210,
        212, 213, 215, 216, 217, 218, 222, 224, 225, 226, 230, 235, 238, 240, 241, 242,
        244, 245, 247, 248, 249, 250, 252, 253, 255, 259, 260, 261, 263, 264, 265, 266,
        270, 272, 273, 274, 276, 277, 279, 280, 281, 282, 286, 288, 289, 290, 294, 299,
        300, 301, 303, 307, 310, 315, 318, 320, 321, 322, 326, 331, 334, 336, 337, 338,
        340, 341, 343, 344, 345, 346, 350, 352, 353, 354, 358, 363, 364, 365, 367, 371,
        374, 379, 382, 384, 385, 386, 390, 395, 396, 397, 399, 403, 406, 411, 412, 413,
        415, 419, 420, 421, 423, 424, 425, 426, 428, 429, 431, 435, 438, 443, 446, 448,
        449, 450, 454, 459, 462, 464, 465, 466, 468, 469, 471, 472, 473, 474, 478, 480,
        481, 482, 486, 491, 494, 496, 497, 498, 500, 501, 503, 504, 505, 506, 508, 509,
        511, 515, 516, 517, 519, 520, 521, 522, 526, 528, 529, 530, 532, 533, 535, 536,
        537, 538, 542, 544, 545, 546, 550, 555, 558, 560, 561, 562, 564, 565, 567, 568,
        569, 570, 572, 573, 575, 579, 580, 581, 583, 584, 585, 586, 588, 589, 591, 595,
        598, 603, 604, 605, 607, 611, 612, 613, 615, 616, 617, 618, 622, 624, 625, 626,
        628, 629, 631, 632, 633, 634, 636, 637, 639, 643, 644, 645, 647, 648, 649, 650,
        654, 656, 657, 658, 660, 661, 663, 664, 665, 666, 670, 672, 673, 674, 678, 683,

        684, 689, 692, 696, 698, 699, 701, 702, 703, 704, 706, 707, 708,
        712, 714, 715, 717, 718, 719, 720, 722, 723, 725, 726, 727, 729,
        733, 734, 735, 736, 738, 739, 740, 744, 746, 747, 748, 753, 756,
        760, 762, 763, 765, 766, 767, 768, 770, 771, 772, 776, 778, 779,
        780, 785, 788, 792, 794, 795, 796, 801, 805, 806, 807, 809
    };

    const unsigned char p1insertion_cc_impl::s1_modulation_patterns[8][8] = 
    {
        {0x12, 0x47, 0x21, 0x74, 0x1D, 0x48, 0x2E, 0x7B},
        {0x47, 0x12, 0x74, 0x21, 0x48, 0x1D, 0x7B, 0x2E},
        {0x21, 0x74, 0x12, 0x47, 0x2E, 0x7B, 0x1D, 0x48},
        {0x74, 0x21, 0x47, 0x12, 0x7B, 0x2E, 0x48, 0x1D},
        {0x1D, 0x48, 0x2E, 0x7B, 0x12, 0x47, 0x21, 0x74},
        {0x48, 0x1D, 0x7B, 0x2E, 0x47, 0x12, 0x74, 0x21},
        {0x2E, 0x7B, 0x1D, 0x48, 0x21, 0x74, 0x12, 0x47},
        {0x7B, 0x2E, 0x48, 0x1D, 0x74, 0x21, 0x47, 0x12}
    };

    const unsigned char p1insertion_cc_impl::s2_modulation_patterns[16][32] = 
    {
        {0x12, 0x1D, 0x47, 0x48, 0x21, 0x2E, 0x74, 0x7B, 0x1D, 0x12, 0x48, 0x47, 0x2E, 0x21, 0x7B, 0x74,
         0x12, 0xE2, 0x47, 0xB7, 0x21, 0xD1, 0x74, 0x84, 0x1D, 0xED, 0x48, 0xB8, 0x2E, 0xDE, 0x7B, 0x8B},
        {0x47, 0x48, 0x12, 0x1D, 0x74, 0x7B, 0x21, 0x2E, 0x48, 0x47, 0x1D, 0x12, 0x7B, 0x74, 0x2E, 0x21,
         0x47, 0xB7, 0x12, 0xE2, 0x74, 0x84, 0x21, 0xD1, 0x48, 0xB8, 0x1D, 0xED, 0x7B, 0x8B, 0x2E, 0xDE},
        {0x21, 0x2E, 0x74, 0x7B, 0x12, 0x1D, 0x47, 0x48, 0x2E, 0x21, 0x7B, 0x74, 0x1D, 0x12, 0x48, 0x47,
         0x21, 0xD1, 0x74, 0x84, 0x12, 0xE2, 0x47, 0xB7, 0x2E, 0xDE, 0x7B, 0x8B, 0x1D, 0xED, 0x48, 0xB8},
        {0x74, 0x7B, 0x21, 0x2E, 0x47, 0x48, 0x12, 0x1D, 0x7B, 0x74, 0x2E, 0x21, 0x48, 0x47, 0x1D, 0x12,
         0x74, 0x84, 0x21, 0xD1, 0x47, 0xB7, 0x12, 0xE2, 0x7B, 0x8B, 0x2E, 0xDE, 0x48, 0xB8, 0x1D, 0xED},
        {0x1D, 0x12, 0x48, 0x47, 0x2E, 0x21, 0x7B, 0x74, 0x12, 0x1D, 0x47, 0x48, 0x21, 0x2E, 0x74, 0x7B,
         0x1D, 0xED, 0x48, 0xB8, 0x2E, 0xDE, 0x7B, 0x8B, 0x12, 0xE2, 0x47, 0xB7, 0x21, 0xD1, 0x74, 0x84},
        {0x48, 0x47, 0x1D, 0x12, 0x7B, 0x74, 0x2E, 0x21, 0x47, 0x48, 0x12, 0x1D, 0x74, 0x7B, 0x21, 0x2E,
         0x48, 0xB8, 0x1D, 0xED, 0x7B, 0x8B, 0x2E, 0xDE, 0x47, 0xB7, 0x12, 0xE2, 0x74, 0x84, 0x21, 0xD1},
        {0x2E, 0x21, 0x7B, 0x74, 0x1D, 0x12, 0x48, 0x47, 0x21, 0x2E, 0x74, 0x7B, 0x12, 0x1D, 0x47, 0x48,
         0x2E, 0xDE, 0x7B, 0x8B, 0x1D, 0xED, 0x48, 0xB8, 0x21, 0xD1, 0x74, 0x84, 0x12, 0xE2, 0x47, 0xB7},
        {0x7B, 0x74, 0x2E, 0x21, 0x48, 0x47, 0x1D, 0x12, 0x74, 0x7B, 0x21, 0x2E, 0x47, 0x48, 0x12, 0x1D,
         0x7B, 0x8B, 0x2E, 0xDE, 0x48, 0xB8, 0x1D, 0xED, 0x74, 0x84, 0x21, 0xD1, 0x47, 0xB7, 0x12, 0xE2},
        {0x12, 0xE2, 0x47, 0xB7, 0x21, 0xD1, 0x74, 0x84, 0x1D, 0xED, 0x48, 0xB8, 0x2E, 0xDE, 0x7B, 0x8B,
         0x12, 0x1D, 0x47, 0x48, 0x21, 0x2E, 0x74, 0x7B, 0x1D, 0x12, 0x48, 0x47, 0x2E, 0x21, 0x7B, 0x74},
        {0x47, 0xB7, 0x12, 0xE2, 0x74, 0x84, 0x21, 0xD1, 0x48, 0xB8, 0x1D, 0xED, 0x7B, 0x8B, 0x2E, 0xDE,
         0x47, 0x48, 0x12, 0x1D, 0x74, 0x7B, 0x21, 0x2E, 0x48, 0x47, 0x1D, 0x12, 0x7B, 0x74, 0x2E, 0x21},
        {0x21, 0xD1, 0x74, 0x84, 0x12, 0xE2, 0x47, 0xB7, 0x2E, 0xDE, 0x7B, 0x8B, 0x1D, 0xED, 0x48, 0xB8,
         0x21, 0x2E, 0x74, 0x7B, 0x12, 0x1D, 0x47, 0x48, 0x2E, 0x21, 0x7B, 0x74, 0x1D, 0x12, 0x48, 0x47},
        {0x74, 0x84, 0x21, 0xD1, 0x47, 0xB7, 0x12, 0xE2, 0x7B, 0x8B, 0x2E, 0xDE, 0x48, 0xB8, 0x1D, 0xED,
         0x74, 0x7B, 0x21, 0x2E, 0x47, 0x48, 0x12, 0x1D, 0x7B, 0x74, 0x2E, 0x21, 0x48, 0x47, 0x1D, 0x12},
        {0x1D, 0xED, 0x48, 0xB8, 0x2E, 0xDE, 0x7B, 0x8B, 0x12, 0xE2, 0x47, 0xB7, 0x21, 0xD1, 0x74, 0x84,
         0x1D, 0x12, 0x48, 0x47, 0x2E, 0x21, 0x7B, 0x74, 0x12, 0x1D, 0x47, 0x48, 0x21, 0x2E, 0x74, 0x7B},
        {0x48, 0xB8, 0x1D, 0xED, 0x7B, 0x8B, 0x2E, 0xDE, 0x47, 0xB7, 0x12, 0xE2, 0x74, 0x84, 0x21, 0xD1,
         0x48, 0x47, 0x1D, 0x12, 0x7B, 0x74, 0x2E, 0x21, 0x47, 0x48, 0x12, 0x1D, 0x74, 0x7B, 0x21, 0x2E},
        {0x2E, 0xDE, 0x7B, 0x8B, 0x1D, 0xED, 0x48, 0xB8, 0x21, 0xD1, 0x74, 0x84, 0x12, 0xE2, 0x47, 0xB7,
         0x2E, 0x21, 0x7B, 0x74, 0x1D, 0x12, 0x48, 0x47, 0x21, 0x2E, 0x74, 0x7B, 0x12, 0x1D, 0x47, 0x48},
        {0x7B, 0x8B, 0x2E, 0xDE, 0x48, 0xB8, 0x1D, 0xED, 0x74, 0x84, 0x21, 0xD1, 0x47, 0xB7, 0x12, 0xE2,
         0x7B, 0x74, 0x2E, 0x21, 0x48, 0x47, 0x1D, 0x12, 0x74, 0x7B, 0x21, 0x2E, 0x47, 0x48, 0x12, 0x1D}
    };

  } /* namespace dvbt2 */
} /* namespace gr */
