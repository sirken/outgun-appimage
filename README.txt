*******************************************************************************
          
    OUTGUN

 A 2D-graphics, 32-player multiplayer, fast-paced capture-the-flag game!

*******************************************************************************
                        [ snapshots of development source packages 2011-03-12 ]
                                                     [ re-released 2020-03-19 ]


-------------------------
Project homepages:
-------------------------

Outgun 1.0 page
   http://outgun.mbnet.fi/1.0/

Outgun website
   http://outgun.mbnet.fi/

Brazilian Outgun website
   http://outgun.sf.net/


-------------------------
Notes for 1.0:
-------------------------

See the HTML help in the doc directory for full documentation.

All available maps aren't used on a server by default. You can select from the
cmaps directory the maps you want to use, and copy them to the maps directory.
Visit http://outgun.mbnet.fi/extras/1.0.html for more maps, and also sound
and graphics themes.

If you are an author of one of the maps on this package, and want your map
removed from the distribution or updated, or the credits changed, contact us at
outgun@mbnet.fi. Also feel free to send us new maps to be included with the
game. Especially good ones. ;)

This game is free software under GNU GPL. See the file 'COPYING' for more
details. Download the sources from http://outgun.mbnet.fi/

There most certainly are at least some bugs in this program, but when a
non-testing version is released we aren't aware of any verified ones. See
http://outgun.mbnet.fi/1.0/known_issues.html for a list of known problems,
including bugs when they are found. If you find a new bug, please report it!
The quality of future versions depends on you.


-------------------------
CREDITS:
-------------------------

Programming for 0.5.0 by
   Fabiana Cecin (Spinal) <fcecin@inf.ufrgs.br>
   Random Name Routine(TM) by Renato Hentschke

Programming for 1.0 by
   Niko Ritari (Nix) <npr1@suomi24.fi>
   Jani Rivinoja (Huntta) <janir@mbnet.fi>
   Special player collision effects by Fabiana Cecin
   Bots by Peter Kosyh (Gloomy), with modifications by Niko Ritari

Graphics themes by
   Jani Rivinoja
   Joonas Rivinoja
   Thomaz de Oliveira dos Reis (ThOR27)
   Renato Felix and Thales Zajdsznajder

Fonts by
   Jani Rivinoja

Sound theme by
   Visa-Valtteri Pimiä <visa.pimia@www.fi>

Translations by
   Finish - Jani Rivinoja and Niko Ritari
   Portuguese (BR) - Thomaz de Oliveira dos Reis and Caio Monteiro (Nosferatu)
   Italian - Alessandro Ferrentino (Lo Scassatore)

Maps by
   Cebs, coiote, Devil, evilKaioh, Flyer, Gloomy, Huntta, jarule, Kiss, Luque,
   Nosferatu, PHiN, Rubens, shadow, Slim, Spinal, ThOR27, th3b3st

1.0.0 beta and later bug reports (in decreasing order of bugs reported)
   ThOR27 (very much deserving of his own line),
   Spinal, Nosferatu, Joonas, PHiN, coiote, rFrota, K4, Zigue, Syrus, Miague,
   SnoOpY

This game uses these libraries:
 * Allegro - http://alleg.sourceforge.net/
 * HawkNL - http://www.hawksoft.com/hawknl/
 * Pthreads-win32 - http://sources.redhat.com/pthreads-win32/

The Windows executable and DLLs have been packed to almost 50% of their
original size using UPX - http://upx.sourceforge.net/

The HawkNL NL.dll shipped with the Windows build of Outgun is modified by Nix
to avoid some problems (sockets that are left open with at least Windows 98 SE,
and "address already in use"). Sources for the modified version are available
at http://outgun.mbnet.fi/HawkNL168src_Nix.zip
