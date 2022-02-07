/*$*********************************************************
$*
$* This code has been taken from DDJ 11/93, from an
$* article by Pawel Szczerbina.
$*
$* Password encryption routines follow.
$* Converted to C from Barry Nance's Pascal
$* prog published in the March -93 issue of Byte.
$*
$* Adapted to be useable for ncpfs by
$* Volker Lendecke <lendecke@namu01.gwdg.de> in
$* October 1995.
$*
$* Stolen to be useable for mars_nwe by
$* Martin Stover <mstover@freeway.de> in
$* Dezember 1995.
$**********************************************************/

/****************************************************************************

I read that Novell is not very open when it comes to technical details
of the Netware Core Protocol. This might be especially true for the
encryption stuff. I took the necessary code from Dr. Dobb's Journal
11/93, Undocumented Corner. I asked Jon Erickson <jon@ddj.com> about
the legal status of this piece of code:


---
Date: Thu, 12 Oct 1995 13:44:18 +0100
From: Volker Lendecke <lendecke>
To: jon@ddj.com
Subject: legal status of your source code?


Hello!

I hope that you're the right one to write to, you are the first on your WWW
server. If you are not, could you please forward this message to the right
person? Thanks.

I'm currently exploring the possibility to write a free (in the GNU GPL
sense) NCP filesystem, which would allow me to access a novell server
transparently. For that I would like to use the encryption functions you
published in DDJ 11/93, Undocumented Corner. I would make some cosmetic
changes, such as other indentations, minor code changes and so on. But I do
not know if that allows me to publish this code under GPL. One alternative
would be to publish a diff against your listing, but that would probably
contain much of your code as well, and it would be very inconvenient for
the average user.

I think that you have some kind of standard procedure for such a
case. Please tell me what I should do.

Many thanks in advance,

    Volker

   +=================================================================+
   ! Volker Lendecke               Internet: lendecke@namu01.gwdg.de !
   ! D-37081 Goettingen, Germany                                     !
   +=================================================================+

--


I got the following answer:

---
From: Jon Erickson <jon@ddj.com>
X-Mailer: SCO System V Mail (version 3.2)
To: lendecke@namu01.gwdg.de
Subject: Re: legal status of your source code?
Date: Thu, 12 Oct 95 5:42:56 PDT

Volker,
Code from Dr. Dobb's Journal related articles is provided for
anyone to use. Clearly, the author of the article should be
given credit.
Jon Erickson

---

With this answer in mind, I took the code and made it a bit more
C-like. The original seemed to be translated by a mechanical pascal->c
translator. Jon's answer encouraged me to publish nwcrypt.c under the
GPL. If anybody who knows more about copyright and sees any problems
with this, please tell me.
****************************************************************************/

/******************* Data types ***************************/
typedef unsigned char buf32[32];
typedef unsigned char buf16[16];
typedef unsigned char buf8[8];
typedef unsigned char buf4[4];
typedef unsigned char u8;

static u8 encrypttable[256] =
{0x7,0x8,0x0,0x8,0x6,0x4,0xE,0x4,0x5,0xC,0x1,0x7,0xB,0xF,0xA,0x8,
 0xF,0x8,0xC,0xC,0x9,0x4,0x1,0xE,0x4,0x6,0x2,0x4,0x0,0xA,0xB,0x9,
 0x2,0xF,0xB,0x1,0xD,0x2,0x1,0x9,0x5,0xE,0x7,0x0,0x0,0x2,0x6,0x6,
 0x0,0x7,0x3,0x8,0x2,0x9,0x3,0xF,0x7,0xF,0xC,0xF,0x6,0x4,0xA,0x0,
 0x2,0x3,0xA,0xB,0xD,0x8,0x3,0xA,0x1,0x7,0xC,0xF,0x1,0x8,0x9,0xD,
 0x9,0x1,0x9,0x4,0xE,0x4,0xC,0x5,0x5,0xC,0x8,0xB,0x2,0x3,0x9,0xE,
 0x7,0x7,0x6,0x9,0xE,0xF,0xC,0x8,0xD,0x1,0xA,0x6,0xE,0xD,0x0,0x7,
 0x7,0xA,0x0,0x1,0xF,0x5,0x4,0xB,0x7,0xB,0xE,0xC,0x9,0x5,0xD,0x1,
 0xB,0xD,0x1,0x3,0x5,0xD,0xE,0x6,0x3,0x0,0xB,0xB,0xF,0x3,0x6,0x4,
 0x9,0xD,0xA,0x3,0x1,0x4,0x9,0x4,0x8,0x3,0xB,0xE,0x5,0x0,0x5,0x2,
 0xC,0xB,0xD,0x5,0xD,0x5,0xD,0x2,0xD,0x9,0xA,0xC,0xA,0x0,0xB,0x3,
 0x5,0x3,0x6,0x9,0x5,0x1,0xE,0xE,0x0,0xE,0x8,0x2,0xD,0x2,0x2,0x0,
 0x4,0xF,0x8,0x5,0x9,0x6,0x8,0x6,0xB,0xA,0xB,0xF,0x0,0x7,0x2,0x8,
 0xC,0x7,0x3,0xA,0x1,0x4,0x2,0x5,0xF,0x7,0xA,0xC,0xE,0x5,0x9,0x3,
 0xE,0x7,0x1,0x2,0xE,0x1,0xF,0x4,0xA,0x6,0xC,0x6,0xF,0x4,0x3,0x0,
 0xC,0x0,0x3,0x6,0xF,0x8,0x7,0xB,0x2,0xD,0xC,0x6,0xA,0xA,0x8,0xD};

static buf32 encryptkeys  =
{0x48,0x93,0x46,0x67,0x98,0x3D,0xE6,0x8D,
 0xB7,0x10,0x7A,0x26,0x5A,0xB9,0xB1,0x35,
 0x6B,0x0F,0xD5,0x70,0xAE,0xFB,0xAD,0x11,
 0xF4,0x47,0xDC,0xA7,0xEC,0xCF,0x50,0xC0};

#include "nwcrypt.h"
static void
shuffle1(buf32 temp, unsigned char *target)
{
	short b4;
	unsigned char b3;
	int s, b2, i;

	b4 = 0;

	for (b2 = 0; b2 <= 1; ++b2)
	{
		for (s = 0; s <= 31; ++s)
		{
			b3 = (temp[s]+b4) ^ (temp[(s+b4)&31] - encryptkeys[s]);
			b4 = b4 + b3;
			temp[s] = b3;
		}
	}

	for (i = 0; i <= 15; ++i) {
		target[i] =    encrypttable[temp[ 2*i    ]]
			    | (encrypttable[temp[ 2*i + 1]] << 4);
	}
}


void
shuffle(unsigned char *lon, const unsigned char *buf, int buflen,
	unsigned char *target)
{
	int b2, d, s;
	buf32 temp;

	while (   (buflen > 0)
	       && (buf[buflen - 1] == 0)) {
		buflen = buflen - 1;
	}

	for (s = 0; s < 32; s++) {
		temp[s] = 0;
	}

	d = 0;
	while (buflen >= 32)
	{
		for (s = 0; s <= 31; ++s)
		{
			temp[s] = temp[s] ^ buf[d];
			d = d + 1;
		}
		buflen = buflen - 32;
	}
	b2 = d;
	if (buflen > 0)
	{
		for (s = 0; s <= 31; ++s)
		{
			if (d + buflen == b2)
			{
				b2 = d;
				temp[s] = temp[s] ^ encryptkeys[s];
			} else {
				temp[s] = temp[s] ^ buf[b2];
				b2 = b2 + 1;
			}
		}
	}

	for (s = 0; s <= 31; ++s)
		temp[s] = temp[s] ^ lon[s & 3];

	shuffle1(temp,target);
}


void
nw_encrypt(unsigned char *fra,unsigned char *buf,unsigned char *til)
{
	buf32 k;
	int s;

	shuffle(&(fra[0]), buf, 16, &(k[ 0]));
	shuffle(&(fra[4]), buf, 16, &(k[16]));

	for (s = 0; s <= 15; ++s)
		k[s] = k[s] ^ k[31 - s];

	for (s = 0; s <= 7; ++s)
		til[s] = k[s] ^ k[15 - s];
}


