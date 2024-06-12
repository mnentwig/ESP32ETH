close all;
pkg load statistics;
if (0)
a = load('slowFiltered.txt');
b = load('midLowFiltered.txt');
c = load('midHighFiltered.txt');
d = load('fullFiltered.txt');

figure(); hold on; grid on;
plot(a, 'k');
plot(b, 'b');
plot(c, 'g');
plot(d, 'r');
legend('slow', 'midLow', 'midHigh', 'full');
end
figure(); hist(a, 300); title('slow');
figure(); hist(b, 300); title('midLow');
figure(); hist(c, 300); title('midHigh');
figure(); hist(d, 300); title('full');

figure(); cdfplot(a); title('slow');
figure(); cdfplot(b); title('midLow');
figure(); cdfplot(c); title('midHigh');
figure(); cdfplot(d); title('full');
