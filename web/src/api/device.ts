const API_URL = 'http://localhost:3000';

export const disableWifiAction = async (): Promise<void> => {
    try {
        const res = await fetch(`${API_URL}/disable-wifi`);
        if (!res.ok) {
            throw new Error();
        }
    } catch (error) {
        console.error(error);
    }
};

export const rotateDisplayAction = async (): Promise<void> => {
    try {
        const res = await fetch(`${API_URL}/rotate-display`);
        if (!res.ok) {
            throw new Error();
        }
    } catch (error) {
        console.error(error);
    }
};

export const refreshDisplayAction = async (): Promise<void> => {
    try {
        const res = await fetch(`${API_URL}/refresh-display`);
        if (!res.ok) {
            throw new Error();
        }
    } catch (error) {
        console.error(error);
    }
};